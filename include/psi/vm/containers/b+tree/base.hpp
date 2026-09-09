#pragma once
////////////////////////////////////////////////////////////////////////////////
///
/// The node pool and everything about a b+tree that does not depend on the
/// key type: node headers and slots, the free list, depth and size
/// bookkeeping, and the two iterators expressed purely in those terms.
///
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "vm_vector.hpp"

#include <psi/vm/align.hpp>
#include <psi/vm/allocation.hpp>
#include <psi/vm/containers/komparator.hpp>
#include <psi/vm/containers/lookup.hpp>
#include <psi/vm/containers/heap_vector.hpp>

#include <psi/build/attributes.hpp>
#include <psi/build/disable_warnings.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>
#include <boost/integer.hpp>
// pdqsort included transitively via komparator.hpp
#include <boost/stl_interfaces/iterator_interface.hpp>
#if 0 // reexamining...
#include <boost/stl_interfaces/sequence_container_interface.hpp>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <std_fix/const_iterator.hpp>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

PSI_WARNING_DISABLE_PUSH()
PSI_WARNING_MSVC_DISABLE( 4127 ) // conditional expression is constant
PSI_WARNING_MSVC_DISABLE( 5030 ) // unrecognized attribute


// LookupType now shared from lookup.hpp

template <typename K, bool transparent_comparator, typename StoredKeyType>
concept InsertableType = ( transparent_comparator && std::is_convertible_v<K, StoredKeyType> ) || std::is_same_v<StoredKeyType, K>;


template <typename T>                                  constexpr bool is_statically_sized   { true };
template <typename T> requires requires{ T{}.size(); } constexpr bool is_statically_sized<T>{ T{}.size() != 0 };

// Byte limit + eligibility live in lookup.hpp (shared, measured constants);
// node size is a compile-time constant here so the dispatch is compile-time.
template <typename Comparator, typename Key, std::uint32_t maximum_array_length>
constexpr bool use_linear_search_for_sorted_array
{
    ( linear_search_eligible<Comparator, Key>                           ) &&
    ( maximum_array_length * sizeof( Key ) <= linear_search_byte_limit  ) &&
    ( is_statically_sized<Key>                                          )
}; // use_linear_search_for_sorted_array


// utility to help avoid having to write custom move ctors/assignments when having
// non-owning unique pointers as members
template <typename T>
struct [[ clang::trivial_abi ]] unique_nonowned_ptr {
    constexpr unique_nonowned_ptr() noexcept = default;
    constexpr unique_nonowned_ptr( T * const p ) noexcept : ptr{ p } {}
    constexpr ~unique_nonowned_ptr() noexcept = default;
    constexpr unique_nonowned_ptr( unique_nonowned_ptr && other ) noexcept : ptr{ std::exchange( other.ptr, nullptr ) } {}
    constexpr unique_nonowned_ptr & operator=( unique_nonowned_ptr && other ) noexcept { ptr = std::exchange( other.ptr, nullptr ); return *this; }
    constexpr unique_nonowned_ptr & operator=( T * const other ) noexcept { ptr = other; return *this; }
    [[ gnu::pure ]] constexpr bool operator==( unique_nonowned_ptr const & other ) const noexcept = default;
    [[ gnu::pure ]] constexpr bool operator==( T const * const other ) const noexcept { return ptr == other; }
    [[ gnu::pure ]] constexpr T       * get()       noexcept { return ptr; }
    [[ gnu::pure ]] constexpr T const * get() const noexcept { return ptr; }
    [[ gnu::pure ]] constexpr T       & operator*()       noexcept { return *ptr; }
    [[ gnu::pure ]] constexpr T const & operator*() const noexcept { return *ptr; }
    [[ gnu::pure ]] constexpr T       * operator->()       noexcept { return ptr; }
    [[ gnu::pure ]] constexpr T const * operator->() const noexcept { return ptr; }

    constexpr explicit operator bool() const noexcept { return ptr != nullptr; }

    T * __restrict ptr{ nullptr };
}; // class unique_nonowned_ptr


////////////////////////////////////////////////////////////////////////////////
// \class bptree_base
////////////////////////////////////////////////////////////////////////////////

class [[ gsl::Owner ]] bptree_base
{
public:
    using size_type       = std::size_t;
    using difference_type = std::make_signed_t<size_type>;
    using storage_result  = err::fallible_result<void, error>;

    static bool constexpr all_bulk_erase_keys_must_exist{ false };

    bptree_base(                      ) noexcept;
    bptree_base( bptree_base const &  );  // COW copy (shares file-mapped pages)
    bptree_base( bptree_base       && ) noexcept = default;
    bptree_base & operator=( bptree_base && ) noexcept = default;

    // Commit dirty pages from this (COW) tree to the target.
    // Selective dirty-node copy: only nodes marked dirty (via mark_dirty() on
    // every mutation) are written into the target.  For memory-backed targets
    // the target's node pool is pre-grown first if the clone allocated new nodes.
    void commit_to( bptree_base & target ) const noexcept;

    [[ gnu::pure ]] bool empty() const noexcept { return BOOST_UNLIKELY( size() == 0 ); }

    void clear() noexcept;

    storage_result map_file  ( auto file, flags::named_object_construction_policy, header_info = {} ) noexcept;
    storage_result map_memory( std::uint32_t initial_capacity_as_number_of_nodes = 0, header_info = {} ) noexcept;

    std::span<std::byte> user_header_data() noexcept;

    bool has_attached_storage() const noexcept { return nodes_.has_attached_storage(); }

protected:
    // TODO make this properly configurable (a template parameter)
#if PSI_VM_BT_PAGE_SIZED_NODES // favoring TLB and disk access related issues
    static constexpr std::uint16_t node_size
    {
#   if ( defined( __APPLE__ ) && defined( __aarch64__ ) ) // Quickfix: CPU and especially RSS memory spike regressions with full Apple Silicon 16kB node sizes, TODO investigate properly
        4096
#   else
        page_size
#   endif
    };
#else // favoring CPU cache & branch prediction (linear scans w/ trivial data and comparators)
    // 512 measured better than 256 at every tree size on x64 and Apple
    // Silicon (in-memory random-find sweep, 100k..32M uint32/uint64 keys):
    // one level less depth at equal-or-better intra-node search cost.
    static constexpr std::uint16_t node_size{ 512 };
#endif

    using depth_t = std::uint8_t;

    template <auto value>
    // ceil( m / 2 )
    static constexpr auto ihalf_ceil{ static_cast<decltype( value )>( ( value + 1 ) / 2 ) };

    struct [[ nodiscard, clang::trivial_abi ]] node_slot // instead of node pointers we store offsets - slots in the node pool
    {
        using value_type = std::uint32_t;
        static node_slot const null;
        value_type index{ static_cast<value_type>( -1 ) }; // in-pool index/offset
        [[ gnu::pure ]] value_type operator*() const noexcept { BOOST_ASSUME( index != null.index ); return index; }
        [[ gnu::pure ]] bool operator==( node_slot const other ) const noexcept { return this->index == other.index; }
        [[ gnu::pure ]] explicit operator bool() const noexcept { return index != null.index; }
    }; // struct node_slot

    struct [[ nodiscard, clang::trivial_abi ]] node_header
    {
        static auto constexpr minimum_header_size{ 3 * sizeof( node_slot ) + 2 * sizeof( /*minimum size_type*/ std::uint8_t ) };
        using size_type = typename boost::uint_value_t<node_size - minimum_header_size>::least;

        // At minimum we need a single-linked/directed list in the vertical/depth
        // and horizontal/breadth directions (and the latter only for the leaf
        // level - to have a connected sorted 'list' of all the values).
        // However having a precise vertical back/up-link (parent_child_idx):
        // * speeds up walks up the tree [as the parent (separator) key slots do
        //   not have to be searched for]
        // * simplifies code (enabling several functions to become independent
        //   of the comparator - no longer need searching - and moved up into
        //   the base b+tree classes)
        // * while at the same time being a negligible overhead considering we
        //   are targeting much larger (page or at least n*cacheline-size sized)
        //   nodes.
        node_slot parent  {};
        node_slot left    {};
        node_slot right   {};
        size_type num_vals{};

        // Compute whether a plain bool for 'dirty' fits in the struct's tail
        // alignment padding without increasing sizeof(node_header). This holds
        // when the raw layout leaves ≥1 byte of slack before the next alignment
        // boundary — e.g. 256B nodes: size_type=uint8_t, 3*4+2*1=14B raw →
        // 16B padded (4B align) → 2B slack → bool fits. 4096B nodes: uint16_t,
        // 3*4+2*2=16B raw → 16B padded → 0B slack → bitfield required.
        static constexpr auto raw_bf_size_ { 3 * sizeof( node_slot ) + 2 * sizeof( size_type ) };
        static constexpr auto padded_size_ { ( raw_bf_size_ + alignof( node_slot ) - 1 ) / alignof( node_slot ) * alignof( node_slot ) };
        static constexpr bool dirty_is_bool{ padded_size_ - raw_bf_size_ >= sizeof( bool ) };

        // Conditional tail: full-width parent_child_idx + bool dirty when there
        // is alignment padding to spare (simpler codegen — no bit extract/insert);
        // otherwise pack both into one size_type via bitfields to preserve node
        // key/value capacity.
        struct bitfield_tail { size_type parent_child_idx : sizeof( size_type ) * CHAR_BIT - 1; size_type dirty : 1; };
        struct bool_tail     { size_type parent_child_idx;                                      bool      dirty;     };
        using tail_t = std::conditional_t<dirty_is_bool, bool_tail, bitfield_tail>;
        tail_t tail{};

      /* TODO
        size_type start; // make keys and children arrays function as devectors: allow empty space at the beginning to avoid moves for smaller borrowings
      */

        [[ gnu::pure ]] bool is_root() const noexcept { return !parent; }

        void mark_dirty() noexcept { tail.dirty = true; }

#   ifndef __clang__ // https://github.com/llvm/llvm-project/issues/36032
        // merely to prevent slicing (in return-node-by-ref cases)
        constexpr explicit node_header( node_header const & ) noexcept = default;
        constexpr explicit node_header( node_header &&      ) noexcept = default;

        constexpr node_header            (                     ) noexcept = default;
        constexpr node_header & operator=( node_header &&      ) noexcept = default;
        constexpr node_header & operator=( node_header const & ) noexcept = default;
#   endif
    }; // struct node_header
    using node_size_type = node_header::size_type;

public: //...mrmlj...needs to be public for does_not_hold_addresses<> specialization at namespace scope
    struct alignas( node_size ) node_placeholder : node_header {};
    struct alignas( node_size ) free_node        : node_header {};
protected:

    // SCARY iterator parts
    struct iter_pos
    {
        node_slot      node        {};
        node_size_type value_offset{};
        [[ gnu::pure ]]
        bool operator==( this iter_pos const self, iter_pos const other ) noexcept { return ( self.node == other.node ) && ( self.value_offset == other.value_offset ); }
    };

    class [[ clang::trivial_abi, gsl::Pointer ]] base_iterator;
    class [[ clang::trivial_abi, gsl::Pointer ]] base_random_access_iterator;

    struct insert_pos_t { node_slot node; node_size_type next_insert_offset; }; // TODO 'merge' with iter_pos

    struct find_pos0 // msvc pass-in-reg facepalm
    {
        node_size_type pos        : ( sizeof( node_size_type ) * CHAR_BIT - 1 );
        node_size_type exact_find : 1;
    };
    struct find_pos1
    {
        node_size_type pos;
        bool           exact_find;
    };

    struct header // or persisted data members
    {
        node_slot             root_;
        node_slot             first_leaf_;
        node_slot             last_leaf_;
        node_slot             free_list_;
        node_slot::value_type free_node_count_{};
        size_t                size_ {};
        depth_t               depth_{};
    }; // struct header

    using node_pool = vm::vm_vector<node_placeholder, node_slot::value_type>;

protected:
    void swap( bptree_base & other ) noexcept;

    base_iterator make_iter( iter_pos ) noexcept;
    base_iterator make_iter( node_slot, node_size_type offset ) noexcept;
    base_iterator make_iter( node_header const &, node_size_type offset ) noexcept;
    base_iterator make_iter( insert_pos_t ) noexcept;

    [[ gnu::pure ]] iter_pos begin_pos() const noexcept;
    [[ gnu::pure ]] iter_pos   end_pos() const noexcept;

    [[ gnu::pure ]] base_iterator begin() noexcept;
    [[ gnu::pure ]] base_iterator end  () noexcept;

    [[ gnu::pure ]] base_random_access_iterator ra_begin() noexcept;
    [[ gnu::pure ]] base_random_access_iterator ra_end  () noexcept;

    [[ gnu::pure ]] size_type size() const noexcept { return hdr().size_; }

    [[ gnu::pure ]] node_slot::value_type used_number_of_nodes() const noexcept;

    PSI_COLD node_header & create_root();

    [[ gnu::pure ]] static bool underflowed( auto const & node ) noexcept { return node.num_vals < node.min_values; }
    [[ gnu::pure ]] static bool can_borrow ( auto const & node ) noexcept { return node.num_vals > node.min_values; }

    [[ gnu::pure ]] depth_t    leaf_level(                     ) const noexcept;
    [[ gnu::pure ]] bool    is_leaf_level( depth_t const level ) const noexcept;

    void free     ( node_header & ) noexcept;
    void free_leaf( node_header & ) noexcept;

    void reserve_additional( node_slot::value_type additional_nodes );
    void reserve           ( node_slot::value_type new_capacity_in_number_of_nodes );

    [[ gnu::pure ]] header       & hdr()       noexcept;
    [[ gnu::pure ]] header const & hdr() const noexcept { return const_cast<bptree_base &>( *this ).hdr(); }

    node_slot first_leaf() const noexcept { return hdr().first_leaf_; }

    static void verify( auto const & node ) noexcept
    {
        BOOST_ASSUME( node.num_vals <= node.max_values );
        // also used for underflowing nodes and (most problematically) for root nodes 'interpreted' as inner nodes...TODO...
        //BOOST_ASSUME( node.num_vals >= node.min_values );
    }
    static void verify_min_max( auto const & node ) noexcept
    { // temporary wrkrnd version for the comment above in version()
        BOOST_ASSUME( node.num_vals <= node.max_values );
        BOOST_ASSUME( node.num_vals >= node.min_values );
    }

    static constexpr auto keys    ( auto       & node ) noexcept { verify( node );                                             return std::span{ node.keys    , static_cast<size_type>( node.num_vals      ) }; }
    static constexpr auto keys    ( auto const & node ) noexcept { verify( node );                                             return std::span{ node.keys    , static_cast<size_type>( node.num_vals      ) }; }
    static constexpr auto children( auto       & node ) noexcept { verify( node ); if constexpr ( requires{ node.children; } ) return std::span{ node.children, static_cast<size_type>( node.num_vals + 1U ) }; else return std::array<node_slot, 0>{}; }
    static constexpr auto children( auto const & node ) noexcept { verify( node ); if constexpr ( requires{ node.children; } ) return std::span{ node.children, static_cast<size_type>( node.num_vals + 1U ) }; else return std::array<node_slot, 0>{}; }

    [[ gnu::pure ]] static constexpr node_size_type num_vals  ( auto const & node ) noexcept { return node.num_vals; }
    [[ gnu::pure ]] static constexpr node_size_type num_chldrn( auto const & node ) noexcept { if constexpr ( requires{ node.children; } ) { BOOST_ASSUME( node.num_vals ); return node.num_vals + 1U; } else return 0; }

    template <auto array>
    static constexpr node_size_type size( auto const & node ) noexcept
    {
        if constexpr ( requires{ &(node.*array) == &node.keys; } ) return num_vals  ( node );
        else                                                       return num_chldrn( node );
    }

    template <auto array>
    static auto rshift( auto & node, node_size_type const start_offset, node_size_type const end_offset ) noexcept
    {
        auto const max{ std::size( node.*array ) };
        BOOST_ASSUME(   end_offset <= max        );
        BOOST_ASSUME( start_offset  < max        );
        BOOST_ASSUME( start_offset  < end_offset );
        auto const begin{ &(node.*array)[ start_offset ] };
        auto const end  { &(node.*array)[   end_offset ] };
        auto const new_begin{ std::shift_right( begin, end, 1 ) };
        BOOST_ASSUME( new_begin == begin + 1 );
        return std::span{ new_begin, end };
    }
    template <auto array> static auto rshift( auto & node, node_size_type const offset ) noexcept { return rshift<array>( node, offset, size<array>( node ) ); }
    template <auto array> static auto rshift( auto & node                              ) noexcept { return rshift<array>( node, 0                           ); }
    template <auto array>
    static auto lshift( auto & node, node_size_type const start_offset, node_size_type const end_offset ) noexcept
    {
        auto const max{ std::size( node.*array ) };
        BOOST_ASSUME(   end_offset <= max        );
        BOOST_ASSUME( start_offset  < max        );
        BOOST_ASSUME( start_offset  < end_offset );
        auto const begin{ &(node.*array)[ start_offset ] };
        auto const end  { &(node.*array)[   end_offset ] };
        auto const new_end{ std::shift_left( begin, end, 1 ) };
        BOOST_ASSUME( new_end == end - 1 );
        return std::span{ begin, new_end };
    }
    template <auto array> static auto lshift( auto & node, node_size_type const offset ) noexcept { return lshift<array>( node, offset, size<array>( node ) ); }
    template <auto array> static auto lshift( auto & node                              ) noexcept { return lshift<array>( node, 0                           ); }

    template <typename N> static void rshift_keys( N & node, auto... args ) noexcept { rshift<&N::keys>( node, args... ); }
    template <typename N> static void lshift_keys( N & node, auto... args ) noexcept { lshift<&N::keys>( node, args... ); }

    template <typename N>
    void rshift_chldrn( N & parent, auto... args ) noexcept {
        auto const shifted_children{ rshift<&N::children>( parent, static_cast<node_size_type>( args )... ) };
        for ( auto ch_slot : shifted_children )
        {
            auto & child{ node( ch_slot ) };
            child.tail.parent_child_idx++;
            child.mark_dirty();
        }
    }
    template <typename N>
    void lshift_chldrn( N & parent, auto... args ) noexcept {
        auto const shifted_children{ lshift<&N::children>( parent, static_cast<node_size_type>( args )... ) };
        for ( auto ch_slot : shifted_children )
        {
            auto & child{ node( ch_slot ) };
            child.tail.parent_child_idx--;
            child.mark_dirty();
        }
    }

    void rshift_sibling_parent_pos( node_header & node ) noexcept;
    void update_right_sibling_link( node_header const & left_node, node_slot left_node_slot ) noexcept;
    void unlink_and_free_node( node_header & node, node_header & cached_left_sibling ) noexcept;
    void unlink_and_free_leaf( node_header & leaf, node_header & cached_left_sibling ) noexcept;

    void unlink_left ( node_header & nd ) noexcept;
    void unlink_right( node_header & nd ) noexcept;
    void link( node_header & left, node_header & right ) const noexcept;

    [[ gnu::sysv_abi ]]
    std::pair<node_slot, node_slot> new_spillover_node_for( node_header & existing_node );

    node_placeholder & new_root( node_slot left_child, node_slot right_child );

    template <typename NodeType, typename SourceNode>
    static NodeType & as( SourceNode & slot ) noexcept
    {
        static_assert( sizeof( NodeType ) == sizeof( slot ) || std::is_same_v<NodeType const, node_header const> );
        return static_cast<NodeType &>( static_cast<node_header &>( slot ) );
    }
    template <typename NodeType, typename SourceNode>
    static NodeType const & as( SourceNode const & slot ) noexcept { return as<NodeType>( const_cast<SourceNode &>( slot ) ); }

    [[ gnu::pure ]] node_placeholder       & node( node_slot const offset )       noexcept { return nodes_[ *offset ]; }
    [[ gnu::pure ]] node_placeholder const & node( node_slot const offset ) const noexcept { return nodes_[ *offset ]; }

    auto       & root()       noexcept { return node( hdr().root_ ); }
    auto const & root() const noexcept { return const_cast<bptree_base &>( *this ).root(); }

    template <typename N> N       & node( node_slot const offset )       noexcept { return as<N>( node( offset ) ); }
    template <typename N> N const & node( node_slot const offset ) const noexcept { return as<N>( node( offset ) ); }

    template <typename N> N       & right( N const & nd )       noexcept { return node<N>( nd.right ); }
    template <typename N> N const & right( N const & nd ) const noexcept { return node<N>( nd.right ); }
    template <typename N> N       & left ( N const & nd )       noexcept { return node<N>( nd.left  ); }
    template <typename N> N const & left ( N const & nd ) const noexcept { return node<N>( nd.left  ); }

    [[ gnu::pure ]] bool      is_my_node( node_header const & ) const noexcept;
    [[ gnu::pure ]] node_slot slot_of   ( node_header const & ) const noexcept;

    static bool full( auto const & node ) noexcept { 
        BOOST_ASSUME( node.num_vals <= node.max_values );
        return node.num_vals == node.max_values;
    }

    [[ nodiscard ]] node_placeholder & new_node();

    template <typename N>
    [[ nodiscard ]] N & new_node() { return as<N>( new_node() ); }

    void reset() noexcept;

    void set_first_leaf( header &, node_slot ) noexcept;
    void set_last_leaf ( header &, node_slot ) noexcept;

private:
    auto header_data() noexcept { return vm::header_data<header>( nodes_.user_header_data() ); }

    header & get_hdr() noexcept;

    void assign_nodes_to_free_pool( node_slot::value_type starting_node ) noexcept;

    void update_leaf_list_ends( node_header & removed_leaf ) noexcept;

    void update_cached_pointers() noexcept;
    void update_dbg_helpers() noexcept;

protected:
    unique_nonowned_ptr<header> p_hdr_; // cached pointer to header in mapped storage (compilers/clang still unable to fully optimize away the vm::header_data code)
    node_pool nodes_;
#ifndef NDEBUG // debugging helpers (undoing type erasure done by contiguous_container_storage_base)
    std::span<node_placeholder const> nodes__{};
#endif
}; // class bptree_base

inline constexpr bptree_base::node_slot const bptree_base::node_slot::null{ static_cast<value_type>( -1 ) };

template <> // tell vm_vector it is safe to persist nodes
inline bool constexpr does_not_hold_addresses<bptree_base::node_placeholder>{ true };

bptree_base::storage_result
bptree_base::map_file( auto const file, flags::named_object_construction_policy const policy, header_info const hdr_info ) noexcept
{
    auto success{ nodes_.map_file( file, policy, hdr_info.add_header<header>() )() };
    if ( success )
    {
        update_cached_pointers();
        if ( nodes_.empty() )
            hdr() = {};
    }
    return success;
}

////////////////////////////////////////////////////////////////////////////////
// \class bptree_base::base_iterator
////////////////////////////////////////////////////////////////////////////////
class [[ clang::trivial_abi, gsl::Pointer ]] bptree_base::base_iterator
{
public:
    constexpr base_iterator() noexcept = default;
    constexpr base_iterator( base_iterator const & ) noexcept = default;

    base_iterator & operator++() noexcept { return ( *this = incremented<true>() ); }
    base_iterator & operator--() noexcept;

    bool operator==( base_iterator const & ) const noexcept;

    base_iterator & operator+=( difference_type n ) noexcept;

    constexpr base_iterator & operator=( base_iterator const & other ) noexcept
    {
#   if defined( NDEBUG ) && __has_builtin( __builtin_constant_p )
        // try to skip the redundant assignment of the nodes pointer yet at the
        // same time support default constructed iterators - so it cannot be
        // skipped unconditionally
        if ( __builtin_constant_p( this->nodes_ ) && this->nodes_ )
            { BOOST_ASSUME( this->nodes_ == other.nodes_ ); }
        else
#   endif
        this->nodes_ = other.nodes_;
        this->pos_   = other.pos_  ;
        return *this;
    }

public: // extensions
    using position = iter_pos;
    [[ gnu::pure ]]
    position const & pos() const noexcept { return pos_; }

protected: friend class bptree_base;
    using nodes_t =
#   ifndef NDEBUG // for bounds checking
        std::span<node_placeholder>;
#   else
        node_placeholder * __restrict;
#   endif

    base_iterator( [[ clang::lifetimebound ]] node_pool &, iter_pos ) noexcept;

    [[ gnu::pure ]] node_header & node() const noexcept;

    template <bool precise_end_handling> [[ using gnu: sysv_abi, hot, const ]]
    base_iterator incremented( this base_iterator ) noexcept; // similarly this pass-by-val to avoid going through the stack

    template <bool precise_end_handling>
    iter_pos at_positive_offset( size_type const n ) const noexcept { return at_positive_offset<precise_end_handling>( nodes_, pos_, n ); }
    iter_pos at_negative_offset( size_type const n ) const noexcept { return at_negative_offset                      ( nodes_, pos_, n ); }

private:
    // noninlined core pass-in-reg functionality for random-access iterator movement
    template <bool precise_end_handling>
    [[ using gnu: hot, leaf, const ]][[ clang::preserve_most ]] static iter_pos at_positive_offset( nodes_t, iter_pos, size_type n ) noexcept;
    [[ using gnu: hot, leaf, const ]][[ clang::preserve_most ]] static iter_pos at_negative_offset( nodes_t, iter_pos, size_type n ) noexcept;

protected:
    // Closest to type D or 'fat pointer' according to this taxonomy
    // https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0773r0.html
    mutable nodes_t  nodes_{};
            iter_pos pos_  {};

private: template <typename T, typename Comparator> friend class bp_tree_impl;
    constexpr base_iterator( nodes_t const nodes, iter_pos const pos ) noexcept : nodes_{ nodes }, pos_{ pos } {}
    void update_pool_ptr( node_pool & ) const noexcept;
}; // class base_iterator


////////////////////////////////////////////////////////////////////////////////
// \class bptree_base::base_random_access_iterator
////////////////////////////////////////////////////////////////////////////////

class [[ clang::trivial_abi, gsl::Pointer ]] bptree_base::base_random_access_iterator : public base_iterator
{
public:
    constexpr base_random_access_iterator() noexcept = default;

    [[ clang::no_sanitize( "unsigned-integer-overflow" ) ]]
    difference_type operator-( base_random_access_iterator const & other ) const noexcept
    {
        BOOST_ASSUME( verify_comparable( *this, other ) );
        return static_cast<difference_type>( this->index_ - other.index_ );
    }
    base_random_access_iterator & operator+=( difference_type const n )       noexcept { return (*this = *this + n); }
    base_random_access_iterator   operator+ ( difference_type const n ) const noexcept
    {
#   if __has_builtin( __builtin_constant_p )
        if ( __builtin_constant_p( n ) ) // has to be in the header (even w/ LTO)
        {
                 if ( n == +1 ) return ++auto(*this);
            else if ( n == -1 ) return --auto(*this);
            else if ( n ==  0 ) return       (*this);
        }
#   endif
        return at_offset( n );
    }

    // same reason for 'precise_end_handling=true' as in operator+
    base_random_access_iterator & operator++(   ) noexcept { static_cast<base_iterator &>( *this ) = base_iterator::incremented<true>(); ++index_; return *this; }
    base_random_access_iterator   operator++(int) noexcept { auto current{ *this }; operator++(); return current; }
    base_random_access_iterator & operator--(   ) noexcept { base_iterator::operator--(); --index_; return *this; }
    base_random_access_iterator   operator--(int) noexcept { auto current{ *this }; operator--(); return current; }

    // should implicitly handle end iterator comparison also (this requires the
    // start_index constructor argument for the construction of end iterators)
    [[ gnu::pure ]] friend auto operator<=>( base_random_access_iterator const & left, base_random_access_iterator const & right ) noexcept
    {
        BOOST_ASSUME( verify_comparable( left, right ) );
        return left.index_ <=> right.index_;
    }
    [[ gnu::pure ]] friend bool operator==( base_random_access_iterator const & left, base_random_access_iterator const & right ) noexcept
    {
        BOOST_ASSUME( verify_comparable( left, right ) );
        return left.index_ == right.index_;
    }

    // the position of the iterator as an index into/offset from the beginning
    // of the container
    [[ gnu::pure ]] size_type absolute_offset() const noexcept { return index_; }

protected:
                                               friend class bptree_base;
    template <typename T, typename Comparator> friend class bp_tree_impl;

    base_random_access_iterator( bptree_base & parent, iter_pos const pos, size_type const start_index ) noexcept
        : base_iterator{ parent.nodes_, pos }, index_{ start_index } {}
    base_random_access_iterator( base_iterator const base, size_type const start_index ) noexcept
        : base_iterator{ base }, index_{ start_index } {}

    size_type index_;
private:
    [[ using gnu: sysv_abi, hot, pure ]]
    base_random_access_iterator at_offset( difference_type n ) const noexcept;

    [[ gnu::pure ]] static bool verify_comparable( base_random_access_iterator const & left, base_random_access_iterator const & right ) noexcept
    {
#   ifdef NDEBUG
        auto const same_source{ left.nodes_ == right.nodes_ };
#   else
        auto const same_source{ left.nodes_.data() == right.nodes_.data() };
#   endif
        BOOST_ASSUME( same_source );
        return same_source;
    }
}; // class base_random_access_iterator

PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
