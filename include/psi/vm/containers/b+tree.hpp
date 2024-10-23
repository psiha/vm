#pragma once

#include <psi/vm/align.hpp>
#include <psi/vm/allocation.hpp>
#include <psi/vm/vector.hpp>

#include <psi/build/disable_warnings.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>
#include <boost/stl_interfaces/iterator_interface.hpp>
#include <boost/stl_interfaces/sequence_container_interface.hpp>

#include <algorithm>
#include <array>
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
PSI_WARNING_MSVC_DISABLE( 5030 ) // unrecognized attribute

namespace detail
{
    template <typename Header>
    [[ gnu::const ]] auto header_data( std::span<std::byte> const hdr_storage ) noexcept
    {
        auto const data           { align_up<alignof( Header )>( hdr_storage.data() ) };
        auto const remaining_space{ hdr_storage.size() - unsigned( data - hdr_storage.data() ) };
        BOOST_ASSERT( remaining_space >= sizeof( Header ) );
        return std::pair{ reinterpret_cast<Header *>( data ), std::span{ data + sizeof( Header ), remaining_space - sizeof( Header ) } };
    }

    template <typename T> static constexpr bool is_simple_comparator{ false };
    template <typename T> static constexpr bool is_simple_comparator<std::less   <T>>{ true };
    template <typename T> static constexpr bool is_simple_comparator<std::greater<T>>{ true };
} // namespace detail

////////////////////////////////////////////////////////////////////////////////
// Modern(ized) attempt at 'automatized' boost::call_traits primarily to support
// efficient transparent comparators & non-inlined generic lookup functions
// which cause neither unnecessary copies of non-trivial types nor pass-by-ref
// of trivial ones.
// Largely still WiP...
// Essentially this is 'explicit IPA SROA'.
// https://gcc.gnu.org/onlinedocs/gccint/passes-and-files-of-the-compiler/inter-procedural-optimization-passes.html
////////////////////////////////////////////////////////////////////////////////

template <typename T>
bool constexpr can_be_passed_in_reg
{
    (
        std::is_trivial_v<T> &&
        ( sizeof( T ) <= 2 * sizeof( void * ) ) // assuming a sane ABI like SysV (ignoring the MS x64 disaster)
    )
#if defined( __GNUC__ ) || defined( __clang__ )
    || // detect SIMD types (this could also produce false positives for large compiler-native vectors that do not fit into the register file)
    requires{ __builtin_convertvector( T{}, T ); }
#endif
    // This is certainly not an exhaustive list/'trait' - certain types that can
    // be passed in reg cannot be detected as such by existing compiler
    // functionality, e.g. Homogeneous Vector Aggregates
    // https://devblogs.microsoft.com/cppblog/introducing-vector-calling-convention
    // users are encouraged to provide specializations for such types.
}; // can_be_passed_in_reg

template <typename T>
struct optimal_const_ref { using type = T const &; };

template <typename Char>
struct optimal_const_ref<std::basic_string<Char>> { using type = std::basic_string_view<char>; };

template <std::ranges::contiguous_range Rng>
struct optimal_const_ref<Rng> { using type = std::span<std::ranges::range_value_t<Rng> const>; };

template <typename T>
struct [[ clang::trivial_abi ]] pass_in_reg
{
    static auto constexpr pass_by_val{ can_be_passed_in_reg<T> };

    using  value_type = T;
    using stored_type = std::conditional_t<pass_by_val, T, optimal_const_ref<T>>;

    constexpr pass_in_reg( T const & u ) noexcept : val{ u } {}

    stored_type val;

    [[ gnu::pure ]] BOOST_FORCEINLINE
    constexpr operator stored_type const &() const noexcept { return val; }
}; // pass_in_reg

template <typename T>
struct [[ clang::trivial_abi ]] pass_rv_in_reg
{
    static auto constexpr pass_by_val{ can_be_passed_in_reg<T> };

    using  value_type = T;
    using stored_type = std::conditional_t<pass_by_val, T, T &&>;

    constexpr pass_rv_in_reg( T && u ) noexcept : val{ std::move( u ) } {} // move for not-trivially-moveable yet trivial_abi types (that can be passed in reg)

    stored_type val;

    [[ gnu::pure ]] BOOST_FORCEINLINE constexpr operator stored_type const & () const noexcept { return            val  ; }
    [[ gnu::pure ]] BOOST_FORCEINLINE constexpr operator stored_type       &&()       noexcept { return std::move( val ); }
}; // pass_rv_in_reg

template <typename K, bool transparent_comparator, typename StoredKeyType>
concept KeyType = ( transparent_comparator && std::is_convertible_v<K, StoredKeyType> ) || std::is_same_v<StoredKeyType, K>;

template <typename T> bool constexpr reg                   { false };
template <typename T> bool constexpr reg<pass_in_reg   <T>>{  true };
template <typename T> bool constexpr reg<pass_rv_in_reg<T>>{  true };

template <typename T>
concept Reg = reg<T>;

// 'Explicit IPA SROA' / pass-in-reg helper end
////////////////////////////////////////////////////////////////////////////////


// user specializations and overloads of this function are allowed
template <typename Comparator, typename Key>
consteval bool use_linear_search_for_sorted_array( [[ maybe_unused ]] std::uint32_t const minimum_array_length, std::uint32_t const maximum_array_length ) noexcept
{
    auto const basic_test
    { 
        detail::is_simple_comparator<Comparator> && 
        std::is_trivially_copyable_v<Key>        &&
        sizeof( Key ) < ( 4 * sizeof( void * ) ) &&
        maximum_array_length < 2048
    };
    if constexpr ( requires{ Key{}.size(); } )
    {
        return basic_test && ( Key{}.size() != 0 );
    }
    return basic_test;
}


////////////////////////////////////////////////////////////////////////////////
// \class bptree_base
////////////////////////////////////////////////////////////////////////////////

class bptree_base
{
public:
    using size_type       = std::size_t;
    using difference_type = std::make_signed_t<size_type>;
    using storage_result  = err::fallible_result<void, error>;

    [[ gnu::pure ]] bool empty() const noexcept { return BOOST_UNLIKELY( size() == 0 ); }

    void clear() noexcept;

    bptree_base( header_info hdr_info = {} ) noexcept;

    std::span<std::byte> user_header_data() noexcept;

    bool has_attached_storage() const noexcept { return nodes_.has_attached_storage(); }

    storage_result map_file( auto const file, flags::named_object_construction_policy const policy ) noexcept
    {
        storage_result success{ nodes_.map_file( file, policy ) };
        if ( std::move( success ) && nodes_.empty() )
            hdr() = {};
        return success;
    }
    storage_result map_memory( std::uint32_t initial_capacity_as_number_of_nodes = 0 ) noexcept;

protected:
    static constexpr auto node_size{ page_size };

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
        using size_type = std::uint16_t;

        // At minimum we need a single-linked/directed list in the vertical/depth
        // and horizontal/breadth directions (and the latter only for the leaf
        // level - to have a connected sorted 'list' of all the values).
        // However having a precise vertical back/up-link (parent_child_idx):
        // * speeds up walks up the tree (as the parent (separator) key slots do
        //   not have to be searched for)
        // * simplifies code (enabling several functions to become independent
        //   of the comparator - no longer need searching - and moved up into
        //   the base bptree classes)
        // * while at the same time being a negligible overhead considering we
        //   are targeting much larger (page size sized) nodes.
        node_slot parent          {};
        node_slot left            {};
        node_slot right           {};
        size_type num_vals        {};
        size_type parent_child_idx{};
      /* TODO
        size_type start; // make keys and children arrays function as devectors: allow empty space at the beginning to avoid moves for smaller borrowings
      */

        [[ gnu::pure ]] bool is_root() const noexcept { return !parent; }

    protected: // merely to prevent slicing (in return-node-by-ref cases)
        constexpr node_header( node_header const & ) noexcept = default;
        constexpr node_header( node_header &&      ) noexcept = default;
    public:
        constexpr node_header(                     ) noexcept = default;
        constexpr node_header & operator=( node_header &&      ) noexcept = default;
        constexpr node_header & operator=( node_header const & ) noexcept = default;
    }; // struct node_header
    using node_size_type = node_header::size_type;

    struct alignas( node_size ) node_placeholder : node_header {};
    struct alignas( node_size ) free_node        : node_header {};

    // SCARY iterator parts
    struct iter_pos
    {
        node_slot      node        {};
        node_size_type value_offset{};
    };
    class base_iterator;
    class base_random_access_iterator;

    struct insert_pos_t { node_slot node; node_size_type next_insert_offset; }; // TODO 'merge' with iter_pos

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

    using node_pool = vm::vector<node_placeholder, node_slot::value_type, false>;

protected:
    void swap( bptree_base & other ) noexcept;

    base_iterator make_iter( iter_pos ) noexcept;

    [[ gnu::pure ]] iter_pos begin_pos() const noexcept;
    [[ gnu::pure ]] iter_pos   end_pos() const noexcept;

    [[ gnu::pure ]] base_iterator begin() noexcept;
    [[ gnu::pure ]] base_iterator end  () noexcept;

    [[ gnu::pure ]] base_random_access_iterator ra_begin() noexcept;
    [[ gnu::pure ]] base_random_access_iterator ra_end  () noexcept;

    [[ gnu::pure ]] size_type size() const noexcept { return hdr().size_; }

    [[ gnu::cold ]] node_header & create_root();

    [[ gnu::pure ]] static bool underflowed( auto const & node ) noexcept { return node.num_vals < node.min_values; }
    [[ gnu::pure ]] static bool can_borrow ( auto const & node ) noexcept { return node.num_vals > node.min_values; }

    [[ gnu::pure ]] depth_t    leaf_level(                     ) const noexcept;
    [[ gnu::pure ]] bool    is_leaf_level( depth_t const level ) const noexcept;

    void free( node_header & ) noexcept;

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

    static constexpr auto keys    ( auto       & node ) noexcept { verify( node );                                             return std::span{ node.keys    , node.num_vals      }; }
    static constexpr auto keys    ( auto const & node ) noexcept { verify( node );                                             return std::span{ node.keys    , node.num_vals      }; }
    static constexpr auto children( auto       & node ) noexcept { verify( node ); if constexpr ( requires{ node.children; } ) return std::span{ node.children, node.num_vals + 1U }; else return std::array<node_slot, 0>{}; }
    static constexpr auto children( auto const & node ) noexcept { verify( node ); if constexpr ( requires{ node.children; } ) return std::span{ node.children, node.num_vals + 1U }; else return std::array<node_slot, 0>{}; }

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
            node( ch_slot ).parent_child_idx++;
    }
    template <typename N>
    void lshift_chldrn( N & parent, auto... args ) noexcept {
        auto const shifted_children{ lshift<&N::children>( parent, static_cast<node_size_type>( args )... ) };
        for ( auto ch_slot : shifted_children )
            node( ch_slot ).parent_child_idx--;
    }

    void rshift_sibling_parent_pos( node_header & node ) noexcept;
    void update_right_sibling_link( node_header const & left_node, node_slot left_node_slot ) noexcept;
    void unlink_node( node_header & node, node_header & cached_left_sibling ) noexcept;

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

    node_placeholder       & node( node_slot const offset )       noexcept { return nodes_[ *offset ]; }
    node_placeholder const & node( node_slot const offset ) const noexcept { return nodes_[ *offset ]; }

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

private:
    auto header_data() noexcept { return detail::header_data<header>( nodes_.user_header_data() ); }

    void assign_nodes_to_free_pool( node_slot::value_type starting_node ) noexcept;

protected:
    node_pool nodes_;
#ifndef NDEBUG
    header const * hdr_{};
#endif
}; // class bptree_base

inline constexpr bptree_base::node_slot const bptree_base::node_slot::null{ static_cast<value_type>( -1 ) };


////////////////////////////////////////////////////////////////////////////////
// \class bptree_base::base_iterator
////////////////////////////////////////////////////////////////////////////////

class bptree_base::base_iterator
{
public:
    constexpr base_iterator() noexcept = default;

    base_iterator & operator++() noexcept;
    base_iterator & operator--() noexcept;

    bool operator==( base_iterator const & ) const noexcept;

public: // extensions
    iter_pos const & pos() const noexcept { return pos_; }

protected:
    friend class bptree_base;

    base_iterator( node_pool &, iter_pos ) noexcept;

    [[ gnu::pure ]] node_header & node() const noexcept;

    mutable
#ifndef NDEBUG // for bounds checking
    std::span<node_placeholder>
#else
    node_placeholder * __restrict
#endif
             nodes_{};
    iter_pos pos_  {};

private:
    template <typename T, typename Comparator>
    friend class bp_tree;
    void update_pool_ptr( node_pool & ) const noexcept;
}; // class base_iterator

////////////////////////////////////////////////////////////////////////////////
// \class bptree_base::base_random_access_iterator
////////////////////////////////////////////////////////////////////////////////

class bptree_base::base_random_access_iterator : public base_iterator
{
protected:
    size_type index_;

                                               friend class bptree_base;
    template <typename T, typename Comparator> friend class bp_tree;

    base_random_access_iterator( bptree_base & parent, iter_pos const pos, size_type const start_index ) noexcept
        : base_iterator{ parent.nodes_, pos }, index_{ start_index } {}

public:
    constexpr base_random_access_iterator() noexcept = default;

    [[ clang::no_sanitize( "unsigned-integer-overflow" ) ]]
    difference_type operator-( base_random_access_iterator const & other ) const noexcept { return static_cast<difference_type>( this->index_ - other.index_ ); }

    base_random_access_iterator & operator+=( difference_type n ) noexcept;

    base_random_access_iterator & operator++(   ) noexcept { base_iterator::operator++(); ++index_; return *this; }
    base_random_access_iterator   operator++(int) noexcept { auto current{ *this }; operator++(); return current; }
    base_random_access_iterator & operator--(   ) noexcept { base_iterator::operator--(); --index_; return *this; }
    base_random_access_iterator   operator--(int) noexcept { auto current{ *this }; operator--(); return current; }

    // should implicitly handle end iterator comparison also (this requires the start_index constructor argument for the construction of end iterators)
    friend constexpr bool operator==( base_random_access_iterator const & left, base_random_access_iterator const & right ) noexcept { return left.index_ == right.index_; }

    auto operator<=>( base_random_access_iterator const & other ) const noexcept { return this->index_ <=> other.index_; }
}; // class base_random_access_iterator


////////////////////////////////////////////////////////////////////////////////
// \class bptree_base_wkey
////////////////////////////////////////////////////////////////////////////////

template <typename Key>
class bptree_base_wkey : public bptree_base
{
private:
    template <typename Impl, typename Tag>
    using iter_impl = boost::stl_interfaces::iterator_interface
    <
#   if !BOOST_STL_INTERFACES_USE_DEDUCED_THIS
        Impl,
#   endif
        Tag,
        Key
    >;

public:
    using key_type   = Key;
    using value_type = key_type; // TODO map support

    // support for non trivial types (for which move and pass-by-ref matters) is WiP, nowhere near complete
    static_assert( std::is_trivial_v<Key> );

    using key_rv_arg    = std::conditional_t<can_be_passed_in_reg<Key>, Key const, pass_rv_in_reg<Key>>;
    using key_const_arg = std::conditional_t<can_be_passed_in_reg<Key>, Key const, pass_in_reg   <Key>>;

    class fwd_iterator;
    class  ra_iterator;

    using       iterator = fwd_iterator;
    using const_iterator = std::basic_const_iterator<iterator>;

public:
    storage_result map_memory( size_type initial_capacity = 0 ) noexcept { return bptree_base::map_memory( node_count_required_for_values( initial_capacity ) ); }
    size_type capacity() const noexcept
    {
        // TODO WiP playground
        auto const n{ nodes_.capacity() };
        if ( !n ) [[ unlikely ]]
            return 0;

        node_slot::value_type inner_nodes{ 0 };
        node_slot::value_type current_level_count{ 1 };
        while ( ( current_level_count * inner_node::max_children ) < ( n - inner_nodes ) )
        {
            inner_nodes         += current_level_count;
            current_level_count *= inner_node::max_children;
        }

        std::uint8_t const depth{ hdr().depth_ };
        std::uint8_t max_inner_node_count{ depth > 1 };
        for ( auto d{ 3 }; d < depth; ++d )
        {
            max_inner_node_count += max_inner_node_count * inner_node::max_children;
        }
        BOOST_ASSUME( max_inner_node_count < n );
        return ( n - max_inner_node_count ) * leaf_node::max_values;
    }

    void reserve_additional( size_type const additional_values ) { bptree_base::reserve_additional( node_count_required_for_values( additional_values ) ); }
    void reserve           ( size_type const new_capacity      ) { bptree_base::reserve           ( node_count_required_for_values( new_capacity      ) ); }

    iterator erase( const_iterator iter ) noexcept;

    // solely a debugging helper (include b+tree_print.hpp)
    void print() const;

protected: // node types
    struct alignas( node_size ) parent_node : node_header
    {
        static auto constexpr storage_space{ node_size - align_up( sizeof( node_header ), alignof( Key ) ) };

        // storage_space       = ( order - 1 ) * sizeof( key ) + order * sizeof( child_ptr )
        // storage_space       = order * szK - szK + order * szC
        // storage_space + szK = order * ( szK + szC )
        // order               = ( storage_space + szK ) / ( szK + szC )
        static constexpr node_size_type order // "m"
        {
            ( storage_space + sizeof( Key ) )
                /
            ( sizeof( Key ) + sizeof( node_slot ) )
        };

        using value_type = Key;

        static node_size_type constexpr max_children{ order };
        static node_size_type constexpr max_values  { max_children - 1 };

        Key       keys    [ max_values   ];
        node_slot children[ max_children ];
    }; // struct inner_node

    struct inner_node : parent_node
    {
        static node_size_type constexpr min_children{ ihalf_ceil<parent_node::max_children> };
        static node_size_type constexpr min_values  { min_children - 1 };

        static_assert( min_children >= 3 );
    }; // struct inner_node

    struct root_node : parent_node
    {
        static auto constexpr min_children{ 2 };
        static auto constexpr min_values  { min_children - 1 };
    }; // struct root_node

    struct alignas( node_size ) leaf_node : node_header
    {
        // TODO support for maps (i.e. keys+values)
        using value_type = Key;

        static node_size_type constexpr storage_space{ node_size - align_up<node_size_type>( sizeof( node_header ), alignof( Key ) ) };
        static node_size_type constexpr max_values   { storage_space / sizeof( Key ) };
        static node_size_type constexpr min_values   { ihalf_ceil<max_values> };

        Key keys[ max_values ];
    }; // struct leaf_node

    static_assert( sizeof( inner_node ) == node_size );
    static_assert( sizeof(  leaf_node ) == node_size );

protected: // split_to_insert and its helpers
    root_node & new_root( node_slot const left_child, node_slot const right_child, key_rv_arg separator_key )
    {
        auto & new_root_node{ as<root_node>( bptree_base::new_root( left_child, right_child ) ) };
        new_root_node.keys    [ 0 ] = std::move( separator_key );
        new_root_node.children[ 0 ] = left_child;
        new_root_node.children[ 1 ] = right_child;
        return new_root_node;
    }

    auto insert_into_new_node
    (
        inner_node & node, inner_node & new_node,
        key_rv_arg value,
        node_size_type const insert_pos, node_size_type const new_insert_pos,
        node_slot const key_right_child
    ) noexcept
    {
        BOOST_ASSUME( bool( key_right_child ) );

        using N = inner_node;

        auto const max{ N::max_values };
        auto const mid{ N::min_values };

        BOOST_ASSUME(     node.num_vals == max );
        BOOST_ASSUME( new_node.num_vals == 0   );

        // old node gets the minimum/median/mid -> the new gets the leftovers (i.e. mid or mid + 1)
        new_node.num_vals = max - mid;

        value_type key_to_propagate;
        if ( new_insert_pos == 0 )
        {
            key_to_propagate = std::move( value );

            move_keys  ( node, mid    , node.num_vals    , new_node, 0 );
            move_chldrn( node, mid + 1, node.num_vals + 1, new_node, 1 );
        }
        else
        {
            key_to_propagate = std::move( node.keys[ mid ] );

            move_keys  ( node, mid + 1, insert_pos    , new_node, 0 );
            move_chldrn( node, mid + 1, insert_pos + 1, new_node, 0 );

            move_keys  ( node, insert_pos    , node.num_vals    , new_node, new_insert_pos     );
            move_chldrn( node, insert_pos + 1, node.num_vals + 1, new_node, new_insert_pos + 1 );

            keys( new_node )[ new_insert_pos - 1 ] = std::move( value );
        }
        insrt_child( new_node, new_insert_pos, key_right_child );

        node.num_vals = mid;

        BOOST_ASSUME( !underflowed( node     ) );
        BOOST_ASSUME( !underflowed( new_node ) );

        return std::make_pair( key_to_propagate, new_insert_pos );
    }

    static auto insert_into_new_node
    (
        leaf_node & node, leaf_node & new_node,
        key_rv_arg value,
        node_size_type const insert_pos, node_size_type const new_insert_pos,
        node_slot const key_right_child
    ) noexcept
    {
        BOOST_ASSUME( !key_right_child );

        using N = leaf_node;

        auto const max{ N::max_values };
        auto const mid{ N::min_values };

        BOOST_ASSUME(     node.num_vals == max );
        BOOST_ASSUME( new_node.num_vals == 0   );

        move_keys( node, mid       , insert_pos, new_node, 0                  );
        move_keys( node, insert_pos, max       , new_node, new_insert_pos + 1 );

        node    .num_vals = mid          ;
        new_node.num_vals = max - mid + 1;

        keys( new_node )[ new_insert_pos ] = std::move( value );
        auto const & key_to_propagate{ new_node.keys[ 0 ] };

        BOOST_ASSUME( !underflowed( node     ) );
        BOOST_ASSUME( !underflowed( new_node ) );

        return std::make_pair( key_to_propagate, static_cast<node_size_type>( new_insert_pos + 1 ) );
    }

    auto insert_into_existing_node( inner_node & node, inner_node & new_node, key_rv_arg value, node_size_type const insert_pos, node_slot const key_right_child ) noexcept
    {
        BOOST_ASSUME( bool( key_right_child ) );

        using N = inner_node;

        auto const max{ N::max_values };
        auto const mid{ N::min_values };

        BOOST_ASSUME(     node.num_vals == max );
        BOOST_ASSUME( new_node.num_vals == 0   );

        value_type key_to_propagate{ std::move( node.keys[ mid - 1 ] ) };

        move_keys  ( node, mid, num_vals  ( node ), new_node, 0 );
        move_chldrn( node, mid, num_chldrn( node ), new_node, 0 );

        rshift_keys  ( node, insert_pos    , mid     );
        rshift_chldrn( node, insert_pos + 1, mid + 1 );

        node    .num_vals = mid;
        new_node.num_vals = max - mid;

        keys       ( node )[ insert_pos ] = std::move( value );
        insrt_child( node, insert_pos + 1, key_right_child );

        BOOST_ASSUME( !underflowed( node     ) );
        BOOST_ASSUME( !underflowed( new_node ) );

        return std::make_pair( std::move( key_to_propagate ), static_cast<node_size_type>( insert_pos + 1 ) );
    }

    static auto insert_into_existing_node( leaf_node & node, leaf_node & new_node, key_rv_arg value, node_size_type const insert_pos, node_slot const key_right_child ) noexcept
    {
        BOOST_ASSUME( !key_right_child );

        using N = leaf_node;

        auto const max{ N::max_values };
        auto const mid{ N::min_values };

        BOOST_ASSUME(     node.num_vals == max );
        BOOST_ASSUME( new_node.num_vals == 0   );

          move_keys( node, mid - 1   , max, new_node, 0 );
        rshift_keys( node, insert_pos, mid              );

        node    .num_vals = mid;
        new_node.num_vals = max - mid + 1;

        keys( node )[ insert_pos ] = std::move( value );
        auto const & key_to_propagate{ new_node.keys[ 0 ] };

        BOOST_ASSUME( !underflowed( node     ) );
        BOOST_ASSUME( !underflowed( new_node ) );

        return std::make_pair( key_to_propagate, static_cast<node_size_type>( insert_pos + 1 ) );
    }

    template <typename N>
    insert_pos_t split_to_insert( N & node_to_split, node_size_type const insert_pos, key_rv_arg value, node_slot const key_right_child )
    {
        auto const max{ N::max_values };
        auto const mid{ N::min_values };
        BOOST_ASSUME( node_to_split.num_vals == max );
        auto [node_slot, new_slot]{ bptree_base::new_spillover_node_for( node_to_split ) };
        auto       p_node    { &node<N>( node_slot ) };
        auto       p_new_node{ &node<N>(  new_slot ) };
        verify( *p_node );
        BOOST_ASSUME( p_node->num_vals == max );
        BOOST_ASSERT
        (
            !p_node->parent || ( node<inner_node>( p_node->parent ).children[ p_node->parent_child_idx ] == node_slot )
        );

        auto const new_insert_pos         { insert_pos - mid };
        bool const insertion_into_new_node{ new_insert_pos >= 0 };
        auto [key_to_propagate, next_insert_pos]{ insertion_into_new_node // we cannot save a reference here because it might get invalidated by the new_node<root_node>() call below
            ? insert_into_new_node     ( *p_node, *p_new_node, std::move( value ), insert_pos, static_cast<node_size_type>( new_insert_pos ), key_right_child )
            : insert_into_existing_node( *p_node, *p_new_node, std::move( value ), insert_pos,                                                key_right_child )
        };

        verify( *p_node     );
        verify( *p_new_node );
        BOOST_ASSUME( p_node->num_vals == mid );

        if ( std::is_same_v<N, leaf_node> && !p_new_node->right ) {
            hdr().last_leaf_ = new_slot;
        }

        // propagate the mid key to the parent
        if ( p_node->is_root() ) [[ unlikely ]] {
            new_root( node_slot, new_slot, std::move( key_to_propagate ) );
        } else {
            auto const key_pos{ static_cast<node_size_type>( p_new_node->parent_child_idx /*it is the _right_ child*/ - 1 ) };
            insert( node<inner_node>( p_node->parent ), key_pos, std::move( key_to_propagate ), new_slot );
        }
        return insertion_into_new_node
            ? insert_pos_t{  new_slot, next_insert_pos }
            : insert_pos_t{ node_slot, next_insert_pos };
    }


protected: // 'other'
    template <typename N>
    insert_pos_t insert( N & target_node, node_size_type const target_node_pos, key_rv_arg v, node_slot const right_child )
    {
        verify( target_node );
        if ( full( target_node ) ) [[ unlikely ]] {
            return split_to_insert( target_node, target_node_pos, std::move( v ), right_child );
        } else {
            ++target_node.num_vals;
            rshift_keys( target_node, target_node_pos );
            target_node.keys[ target_node_pos ] = std::move( v );
            if constexpr ( requires { target_node.children; } ) {
                node_size_type const ch_pos( target_node_pos + /*>right< child*/ 1 );
                rshift_chldrn( target_node, ch_pos );
                this->insrt_child( target_node, ch_pos, right_child );
            }
            return { slot_of( target_node ), static_cast<node_size_type>( target_node_pos + 1 ) };
        }
    }

    iter_pos erase( leaf_node & leaf, node_size_type const leaf_key_offset ) noexcept
    {
        auto & hdr   { this->hdr() };
        auto & depth_{ hdr.depth_ };

        iter_pos next_pos{ slot_of( leaf ), leaf_key_offset };

        lshift_keys( leaf, leaf_key_offset );
        --leaf.num_vals;

        if ( depth_ == 1 ) [[ unlikely ]] // handle 'leaf root' deletion directly to simplify handle_underflow()
        {
            auto & root_{ hdr.root_ };
            BOOST_ASSUME( root_ == slot_of( leaf ) );
            BOOST_ASSUME( leaf.is_root() );
            BOOST_ASSUME( hdr.size_ == leaf.num_vals + 1 );
            BOOST_ASSUME( !leaf.left  );
            BOOST_ASSUME( !leaf.right );
            if ( leaf.num_vals == 0 )
            {
                root_ = {};
                free( leaf );
                --depth_;
                next_pos = bptree_base::end_pos();
            }
        }
        else
        {
            auto p_leaf{ &leaf };
            if ( underflowed( leaf ) )
            {
                BOOST_ASSUME( !leaf.is_root() );
                BOOST_ASSUME( depth_ > 1 );
                next_pos = handle_underflow( leaf, leaf_level() );
                next_pos.value_offset += leaf_key_offset;
                p_leaf                 = &this->leaf( next_pos.node );
                BOOST_ASSUME( next_pos.value_offset <= p_leaf->num_vals );
            }

            if ( leaf_key_offset == p_leaf->num_vals ) // the last value in a node was erased
            {
                if ( !p_leaf->right ) {
                    next_pos = bptree_base::end_pos();
                } else {
                    next_pos.node         = p_leaf->right;
                    next_pos.value_offset = 0;
                }
            }
        }

        --hdr.size_;
        return next_pos;
    }

    // This function only serves the purpose of maintaining the rule about the
    // minimum number of children per node - that rule is actually only
    // 'academic' (for making sure that the performance/complexity guarantees
    // will be maintained) - the tree would operate correctly even without
    // maintaining that invariant (TODO make this an option).
    bool bulk_append_fill_incomplete_leaf( leaf_node & leaf ) noexcept
    {
        auto const missing_keys{ static_cast<node_size_type>( std::max( 0, signed( leaf.min_values ) - leaf.num_vals ) ) };
        if ( missing_keys )
        {
            auto & preceding{ left( leaf ) };
            BOOST_ASSUME( preceding.num_vals + leaf.num_vals >= leaf_node::min_values * 2 );
            std::shift_right( &leaf.keys[ 0 ], &leaf.keys[ leaf.num_vals + missing_keys ], missing_keys );
            this->move_keys( preceding, preceding.num_vals - missing_keys, preceding.num_vals, leaf, 0 );
            leaf     .num_vals += missing_keys;
            preceding.num_vals -= missing_keys;
            return true;
        }
        return false;
    }

    void bulk_append( leaf_node * src_leaf, insert_pos_t rightmost_parent_pos )
    {
        for ( ;; )
        {
            BOOST_ASSUME( !src_leaf->parent );
            auto & rightmost_parent{ inner( rightmost_parent_pos.node ) };
            BOOST_ASSUME( rightmost_parent_pos.next_insert_offset == rightmost_parent.num_vals );
            auto const next_src_slot{ src_leaf->right };
            rightmost_parent_pos = insert
            (
                rightmost_parent,
                rightmost_parent_pos.next_insert_offset,
                key_rv_arg{ /*mrmlj*/Key{ src_leaf->keys[ 0 ] } },
                slot_of( *src_leaf )
            );
            if ( !next_src_slot )
                break;
            src_leaf = &leaf( next_src_slot );
        }
        hdr().last_leaf_ = slot_of( *src_leaf );
        if ( bulk_append_fill_incomplete_leaf( *src_leaf ) )
        {
            // Borrowing from the left sibling happened _after_ src_leaf was
            // already inserted into the parent so we have to update the
            // separator key in the parent (since this is the rightmost leaf we
            // know that the separator key has to be in the immediate parent -
            // no need to call the generic update_separator()).
            auto & prnt{ parent( *src_leaf ) };
            BOOST_ASSUME( src_leaf->parent_child_idx == num_chldrn( prnt ) - 1 );
            keys( prnt ).back() = src_leaf->keys[ 0 ];
        }
    }

    struct bulk_copied_input
    {
        node_slot begin;
        iter_pos  end;
        size_type size;
    };
    template <typename I, typename S, std::ranges::subrange_kind kind>
    bulk_copied_input
    bulk_insert_prepare( std::ranges::subrange<I, S, kind> keys )
    {
        auto constexpr can_preallocate{ kind == std::ranges::subrange_kind::sized };
        if constexpr ( can_preallocate )
            reserve_additional( static_cast<size_type>( keys.size() ) );
        else
            reserve_additional( 42 );
        // w/o preallocation a saved hdr reference could get invalidated
        auto const begin    { can_preallocate ? hdr().free_list_ : slot_of( new_node<leaf_node>() ) };
        auto       leaf_slot{ begin };
        auto       p_keys{ keys.begin() };
        size_type  count{ 0 };
        for ( ;; )
        {
            leaf_node & leaf{ this->leaf( leaf_slot ) };
            BOOST_ASSUME( leaf.num_vals == 0 );
            if constexpr ( can_preallocate ) {
                auto const size_to_copy{ static_cast<node_size_type>( std::min<std::size_t>( leaf.max_values, static_cast<std::size_t>( keys.end() - p_keys ) ) ) };
                BOOST_ASSUME( size_to_copy );
                std::copy_n( p_keys, size_to_copy, leaf.keys );
                leaf.num_vals  = size_to_copy;
                count         += size_to_copy;
                p_keys        += size_to_copy;
            } else {
                while ( ( p_keys != keys.end() ) && ( leaf.num_vals < leaf.max_values ) ) {
                    leaf.keys[ leaf.num_vals++ ] = *p_keys++;
                }
                count += leaf.num_vals;
            }
            --this->hdr().free_node_count_;
            if ( p_keys != keys.end() )
            {
                if constexpr ( can_preallocate ) {
                    leaf_slot = leaf.right;
                } else {
                    auto & new_leaf{ new_node<leaf_node>() };
                    link( leaf, new_leaf );
                    leaf_slot = slot_of( new_leaf );
                }
                BOOST_ASSUME( !!leaf_slot );
            }
            else
            {
                if constexpr ( can_preallocate ) {
                    this->hdr().free_list_ = leaf.right;
                    unlink_right( leaf );
                    BOOST_ASSERT( count == static_cast<size_type>( keys.size() ) );
                    count = static_cast<size_type>( keys.size() ); // eliminate the accumulation code above
                }
                return bulk_copied_input{ begin, { leaf_slot, leaf.num_vals }, count };
            }
        }
        std::unreachable();
    }

    void bulk_insert_into_empty( node_slot const begin_leaf, iter_pos const end_leaf, size_type const total_size )
    {
        BOOST_ASSUME( empty() );
        auto * hdr{ &this->hdr() };
        hdr->root_       = begin_leaf;
        hdr->first_leaf_ = begin_leaf;
        if ( begin_leaf == end_leaf.node ) [[ unlikely ]] // single-node-sized initial insert
        {
            hdr->last_leaf_ = end_leaf.node;
            return;
        }
        auto const & first_root_left { leaf ( begin_leaf      ) };
        auto       & first_root_right{ right( first_root_left ) };
        first_root_right.parent_child_idx = 1;
        hdr->depth_                       = 1;
        auto const first_unconnected_node{ first_root_right.right };
        new_root( begin_leaf, first_root_left.right, key_rv_arg{ /*mrmlj*/Key{ first_root_right.keys[ 0 ] } } ); // may invalidate references
        hdr = &this->hdr();
        BOOST_ASSUME( hdr->depth_ == 2 );
        bulk_append( &leaf( first_unconnected_node ), { hdr->root_, 1 } );
        BOOST_ASSUME( hdr->last_leaf_ == end_leaf.node );
        hdr->size_ = total_size;
    }

    [[ gnu::pure ]]  leaf_node & leaf  ( node_slot const slot ) noexcept { return node< leaf_node>( slot ); }
    [[ gnu::pure ]] inner_node & inner ( node_slot const slot ) noexcept { return node<inner_node>( slot ); }
    [[ gnu::pure ]] inner_node & parent( node_header & child ) noexcept { return inner( child.parent ); }

     leaf_node const & leaf  ( node_slot const slot ) const noexcept { return const_cast<bptree_base_wkey &>( *this ).leaf( slot ); }
    inner_node const & parent( node_header const & child  ) const noexcept { return const_cast<bptree_base_wkey &>( *this ).parent( const_cast<node_header &>( child ) ); }

    void update_separator( leaf_node & leaf, Key const & new_separator ) noexcept
    {
        // the leftmost leaf does not have a separator key (at all)
        if ( !leaf.left ) [[ unlikely ]]
        {
            BOOST_ASSUME( leaf.parent_child_idx == 0 );
            BOOST_ASSUME( hdr().first_leaf_ == slot_of( leaf ) );
            return;
        }
        auto const & separator_key{ leaf.keys[ 0 ] };
        BOOST_ASSUME( separator_key != new_separator );
        // a leftmost child does not have a key (in the immediate parent)
        // (because a left child is strictly less-than its separator key - for
        // the leftmost child there is no key further left that could be
        // greater-or-equal to it)
        auto   parent_child_idx{ leaf.parent_child_idx };
        auto * parent          { &this->parent( leaf ) };
        while ( parent_child_idx == 0 )
        {
            parent           = &this->parent( *parent );
            parent_child_idx = parent->parent_child_idx;
        }
        // can be zero only for the leftmost leaf which was checked for in the
        // loop above and at the beginning of the function
        BOOST_ASSUME( parent_child_idx > 0 );
        auto & parent_key{ parent->keys[ leaf.parent_child_idx - 1 ] };
        BOOST_ASSUME( parent_key == separator_key );
        parent_key = new_separator;
    }

    template <typename N>
    BOOST_NOINLINE
    iter_pos handle_underflow( N & node, depth_t const level ) noexcept
    {
        BOOST_ASSUME( underflowed( node ) );

        auto constexpr parent_node_type{ requires{ node.children; } };
        auto constexpr leaf_node_type  { !parent_node_type };

        BOOST_ASSUME( level > 0 );
        BOOST_ASSUME( !leaf_node_type || level == leaf_level() );
        auto const node_slot{ slot_of( node ) };
        auto & parent{ this->node<inner_node>( node.parent ) };
        verify( parent );

        BOOST_ASSUME( node.num_vals == node.min_values - 1 );
        auto const parent_child_idx   { node.parent_child_idx };
        bool const parent_has_key_copy{ leaf_node_type && ( parent_child_idx > 0 ) };
        auto const parent_key_idx     { parent_child_idx - parent_has_key_copy };
        BOOST_ASSUME( !parent_has_key_copy || parent.keys[ parent_key_idx ] == node.keys[ 0 ] );

        BOOST_ASSUME( parent.children[ parent_child_idx ] == node_slot );
        // the left and right level dlink pointers can point 'across' parents
        // (and so cannot be used to resolve existence of siblings)
        auto const has_right_sibling{ parent_child_idx < ( num_chldrn( parent ) - 1 ) };
        auto const has_left_sibling { parent_child_idx > 0 };
        auto const p_right_sibling  { has_right_sibling ? &right( node ) : nullptr };
        auto const p_left_sibling   { has_left_sibling  ? &left ( node ) : nullptr };

        auto const right_separator_key_idx{ static_cast<node_size_type>( parent_key_idx + parent_has_key_copy ) };
        auto const  left_separator_key_idx{ std::min( static_cast<node_size_type>( right_separator_key_idx - 1 ), parent.num_vals ) }; // (ab)use unsigned wraparound
        auto const p_right_separator_key { has_right_sibling ? &parent.keys[ right_separator_key_idx ] : nullptr };
        auto const p_left_separator_key  { has_left_sibling  ? &parent.keys[  left_separator_key_idx ] : nullptr };

        // save&return the node (and offset) that the underflowed node's values
        // end up
        auto           final_node                     { node_slot };
        node_size_type final_node_original_keys_offset{ 0 };

        BOOST_ASSUME( has_right_sibling || has_left_sibling );
        BOOST_ASSERT( &node != p_left_sibling  );
        BOOST_ASSERT( &node != p_right_sibling );
        BOOST_ASSERT( static_cast<node_header *>( &node ) != static_cast<node_header *>( &parent ) );
        // Borrow from left sibling if possible
        if ( p_left_sibling && can_borrow( *p_left_sibling ) )
        {
            verify( *p_left_sibling );
            BOOST_ASSUME( node.num_vals == node.min_values - 1 );
            node.num_vals++;
            rshift_keys( node );
            BOOST_ASSUME( node.num_vals == node.min_values );
            auto const node_keys{ keys( node ) };
            auto const left_keys{ keys( *p_left_sibling ) };

            if constexpr ( leaf_node_type ) {
                // Move the largest key from left sibling to the current node
                BOOST_ASSUME( parent_has_key_copy );
                node_keys.front() = std::move( left_keys.back() );
                // adjust the separator key in the parent
                BOOST_ASSERT( *p_left_separator_key == node_keys[ 1 ] );
                *p_left_separator_key = node_keys.front();
            } else {
                // Move/rotate the largest key from left sibling to the current node 'through' the parent
                BOOST_ASSERT( left_keys.back()      < *p_left_separator_key );
                BOOST_ASSERT( *p_left_separator_key < node_keys.front()     );
                node_keys.front()     = std::move( *p_left_separator_key );
                *p_left_separator_key = std::move( left_keys.back() );

                rshift_chldrn( node );
                insrt_child( node, 0, children( *p_left_sibling ).back(), node_slot );
            }

            p_left_sibling->num_vals--;
            verify( *p_left_sibling );

            final_node_original_keys_offset = 1;

            BOOST_ASSUME( node.           num_vals == N::min_values );
            BOOST_ASSUME( p_left_sibling->num_vals >= N::min_values );
        }
        // Borrow from right sibling if possible
        else
        if ( p_right_sibling && can_borrow( *p_right_sibling ) )
        {
            verify( *p_right_sibling );
            BOOST_ASSUME( node.num_vals == node.min_values - 1 );
            node.num_vals++;
            auto const node_keys{ keys( node ) };
            if constexpr ( leaf_node_type ) {
                // Move the smallest key from the right sibling to the current node
                auto & leftmost_right_key{ keys( *p_right_sibling ).front() };
                BOOST_ASSUME( *p_right_separator_key == leftmost_right_key ); // yes we expect exact or bitwise equality for key-copies in inner nodes
                node_keys.back() = std::move( leftmost_right_key );
                lshift_keys( *p_right_sibling );
                // adjust the separator key in the parent
                *p_right_separator_key = leftmost_right_key;
            } else {
                // Move/rotate the smallest key from the right sibling to the current node 'through' the parent

                // no comparator in base classes :/
                //BOOST_ASSUME( le( parent.keys[ parent_child_idx ], p_right_sibling->keys[ 0 ] ) );
                //BOOST_ASSERT( le( *( node_keys.end() - 2 ), *p_right_separator_key ) );
                node_keys.back()       = std::move( *p_right_separator_key );
                *p_right_separator_key = std::move( keys( *p_right_sibling ).front() );
                insrt_child( node, num_chldrn( node ) - 1, children( *p_right_sibling ).front(), node_slot );
                lshift_keys  ( *p_right_sibling );
                lshift_chldrn( *p_right_sibling );
            }

            p_right_sibling->num_vals--;
            verify( *p_right_sibling );

            BOOST_ASSUME( node.            num_vals == N::min_values );
            BOOST_ASSUME( p_right_sibling->num_vals >= N::min_values );
        }
        // Merge with left or right sibling
        else
        {
            if ( p_left_sibling ) {
                verify( *p_left_sibling );
                BOOST_ASSUME( parent_has_key_copy == leaf_node_type );
                // Merge node -> left sibling
                final_node                      = node.left;
                final_node_original_keys_offset = p_left_sibling->num_vals;
                merge_right_into_left( *p_left_sibling, node, parent, left_separator_key_idx, parent_child_idx );
            } else {
                verify( *p_right_sibling );
                BOOST_ASSUME( parent_key_idx == 0 );
                // no comparator in base classes :/
                //BOOST_ASSUME( leq( parent.keys[ parent_key_idx ], p_right_sibling->keys[ 0 ] ) );
                // Merge right sibling -> node
                merge_right_into_left( node, *p_right_sibling, parent, right_separator_key_idx, static_cast<node_size_type>( parent_child_idx + 1 ) );
            }

            // propagate underflow
            auto & depth_{ this->hdr().depth_ };
            auto & root_ { this->hdr().root_  };
            if ( parent.is_root() ) [[ unlikely ]]
            {
                BOOST_ASSUME( root_ == slot_of( parent ) );
                BOOST_ASSUME( level == 1     );
                BOOST_ASSUME( depth_ > level ); // 'leaf root' should be handled directly by the special case in erase()
                auto & root{ as<root_node>( parent ) };
                BOOST_ASSUME( !!root.children[ 0 ] );
                if ( underflowed( root ) )
                {
                    root_ = root.children[ 0 ];
                    bptree_base::node<root_node>( root_ ).parent = {};
                    --depth_;
                    free( root );
                }
            }
            else
            if ( underflowed( parent ) )
            {
                BOOST_ASSUME( level > 0      );
                BOOST_ASSUME( level < depth_ );
                handle_underflow( parent, level - 1 );
            }
        }

        return { final_node, final_node_original_keys_offset };
    } // handle_underflow()

    root_node       & root()       noexcept { return as<root_node>( bptree_base::root() ); }
    root_node const & root() const noexcept { return const_cast<bptree_base_wkey &>( *this ).root(); }

    using bptree_base::free;
    void free( leaf_node & leaf ) noexcept {
        auto & first_leaf{ hdr().first_leaf_ };
        if ( first_leaf == slot_of( leaf ) )
        {
            BOOST_ASSUME( !leaf.left );
            first_leaf = leaf.right;
            unlink_right( leaf );
        }
        bptree_base::free( static_cast<node_header &>( leaf ) );
    }

    template <typename N>
    [[ gnu::sysv_abi ]] static
    void move_keys
    (
        N const & source, node_size_type src_begin, node_size_type src_end,
        N       & target, node_size_type tgt_begin
    ) noexcept;
    [[ gnu::sysv_abi ]]
    void move_chldrn
    (
        inner_node const & source, node_size_type src_begin, node_size_type src_end,
        inner_node       & target, node_size_type tgt_begin
    ) noexcept;

    void insrt_child( inner_node & target, node_size_type const pos, node_slot const child_slot, node_slot const cached_target_slot ) noexcept
    {
        BOOST_ASSUME( cached_target_slot == slot_of( target ) );
        auto & child{ node( child_slot ) };
        children( target )[ pos ] = child_slot;
        child.parent              = cached_target_slot;
        child.parent_child_idx    = pos;
    }
    void insrt_child( inner_node & target, node_size_type const pos, node_slot const child_slot ) noexcept
    {
        insrt_child( target, pos, child_slot, slot_of( target ) );
    }

    [[ gnu::sysv_abi ]]
    void merge_right_into_left
    (
         leaf_node & __restrict left, leaf_node & __restrict right,
        inner_node & __restrict parent, node_size_type const parent_key_idx, node_size_type const parent_child_idx
    ) noexcept
    {
        auto constexpr min{ leaf_node::min_values };
        BOOST_ASSUME( right.num_vals >= min - 1 ); BOOST_ASSUME( right.num_vals <= min );
        BOOST_ASSUME( left .num_vals >= min - 1 ); BOOST_ASSUME( left .num_vals <= min );
        BOOST_ASSUME( left .right == slot_of( right ) );
        BOOST_ASSUME( right.left  == slot_of( left  ) );

        std::ranges::move( keys( right ), &keys( left ).back() + 1 );
        left .num_vals += right.num_vals;
        right.num_vals  = 0;
        BOOST_ASSUME( left.num_vals >= 2 * min - 2 );
        BOOST_ASSUME( left.num_vals <= 2 * min - 1 );
        lshift_keys  ( parent, parent_key_idx   );
        lshift_chldrn( parent, parent_child_idx );
        BOOST_ASSUME( parent.num_vals );
        parent.num_vals--;

        unlink_node( right, left );
        verify( left  );
        verify( parent );
    }
    [[ gnu::sysv_abi ]]
    void merge_right_into_left
    (
        inner_node & __restrict left, inner_node & __restrict right,
        inner_node & __restrict parent, node_size_type const parent_key_idx, node_size_type const parent_child_idx
    ) noexcept
    {
        auto constexpr min{ inner_node::min_values };
        BOOST_ASSUME( right.num_vals >= min - 1 ); BOOST_ASSUME( right.num_vals <= min );
        BOOST_ASSUME( left .num_vals >= min - 1 ); BOOST_ASSUME( left .num_vals <= min );

        move_chldrn( right, 0, num_chldrn( right ), left, num_chldrn( left ) );
        auto & separator_key{ parent.keys[ parent_key_idx ] };
        left.num_vals += 1;
        keys( left ).back() = std::move( separator_key );
        std::ranges::move( keys( right ), keys( left ).end() );

        left.num_vals += right.num_vals;
        BOOST_ASSUME( left.num_vals >= left.max_values - 1 ); BOOST_ASSUME( left.num_vals <= left.max_values );
        verify( left );
        unlink_node( right, left );

        lshift_keys  ( parent, parent_key_idx   );
        lshift_chldrn( parent, parent_child_idx );
        BOOST_ASSUME( parent.num_vals );
        parent.num_vals--;
    }

private:
    [[ gnu::const, gnu::noinline ]]
    static node_slot::value_type node_count_required_for_values( size_type const number_of_values ) noexcept
    {
        if ( !number_of_values )
            return 0;
        auto const  leaf_count{ static_cast<node_slot::value_type>( divide_up( number_of_values, /*assuming an 'optimistic' reserve, i.e. for bulk insert*/leaf_node::max_values ) ) };
        auto       total_count{ node_slot::value_type{ 0 } };
        auto       current_level_count{ leaf_count };
        auto       depth{ 1 };
        while ( current_level_count > 1 )
        {
            total_count += current_level_count;
            current_level_count = divide_up( current_level_count, inner_node::min_children ); // pessimistic about inner node utilization
            ++depth;
        }
        // theoretical (+1 since we use a 1-based depth index)
        auto const minimum_height{ static_cast<std::uint8_t>( 1 + std::ceil( std::log(   number_of_values + 1       ) / std::log( inner_node::max_children ) ) - 1 ) };
        auto const maximum_height{ static_cast<std::uint8_t>( 1 +            std::log( ( number_of_values + 1 ) / 2 ) / std::log( inner_node::min_children )       ) };
        BOOST_ASSUME( depth >= minimum_height );
        BOOST_ASSUME( depth <= maximum_height );
        return total_count;
    }
}; // class bptree_base_wkey

////////////////////////////////////////////////////////////////////////////////
// \class bptree_base_wkey::fwd_iterator
////////////////////////////////////////////////////////////////////////////////

template <typename Key>
class bptree_base_wkey<Key>::fwd_iterator
    :
    public base_iterator,
    public iter_impl<fwd_iterator, std::bidirectional_iterator_tag>
{
private:
    using impl = iter_impl<fwd_iterator, std::bidirectional_iterator_tag>;

    using base_iterator::base_iterator;

public:
    constexpr fwd_iterator() noexcept = default;

    Key & operator*() const noexcept
    {
        auto & leaf{ static_cast<leaf_node &>( node() ) };
        BOOST_ASSUME( pos_.value_offset < leaf.num_vals );
        return leaf.keys[ pos_.value_offset ];
    }

    std::span<Key const> get_contiguous_span_and_move_to_next_node() noexcept
    {
        auto & leaf{ static_cast<leaf_node &>( node() ) };
        BOOST_ASSUME( pos_.value_offset < leaf.num_vals );
        std::span<Key const> const span{ &leaf.keys[ pos_.value_offset ], leaf.num_vals - pos_.value_offset };
        if ( leaf.right ) [[ likely ]]
        {
            pos_.node         = leaf.right;
            pos_.value_offset = 0;
        }
        return span;
    }

    constexpr fwd_iterator & operator++() noexcept { return static_cast<fwd_iterator &>( base_iterator::operator++() ); }
    constexpr fwd_iterator & operator--() noexcept { return static_cast<fwd_iterator &>( base_iterator::operator--() ); }
    using impl::operator++;
    using impl::operator--;
}; // class fwd_iterator

////////////////////////////////////////////////////////////////////////////////
// \class bptree_base_wkey::ra_iterator
////////////////////////////////////////////////////////////////////////////////

template <typename Key>
class bptree_base_wkey<Key>::ra_iterator
    :
    public base_random_access_iterator,
    public iter_impl<ra_iterator, std::random_access_iterator_tag>
{
private: friend class bptree_base_wkey<Key>;
    using base_random_access_iterator::base_random_access_iterator;

    leaf_node & node() const noexcept { return static_cast<leaf_node &>( base_random_access_iterator::node() ); }

public:
    constexpr ra_iterator() noexcept = default;

    // TODO deduplicate this w/ fwd_iterator
    Key & operator*() const noexcept
    {
        auto & leaf{ node() };
        BOOST_ASSUME( pos_.value_offset < leaf.num_vals );
        return leaf.keys[ pos_.value_offset ];
    }

    std::span<Key const> get_contiguous_span_and_move_to_next_node() noexcept
    {
        auto & leaf{ static_cast<leaf_node &>( node() ) };
        BOOST_ASSUME( pos_.value_offset < leaf.num_vals );
        std::span<Key const> const span{ &leaf.keys[ pos_.value_offset ], leaf.num_vals - pos_.value_offset };
        index_            += span.size();
        pos_.node          = leaf.right;
        pos_.value_offset  = 0;
        return span;
    }

    ra_iterator & operator+=( difference_type const n ) noexcept { return static_cast<ra_iterator &>( base_random_access_iterator::operator+=( n ) ); }

    ra_iterator & operator++(   ) noexcept { return static_cast<ra_iterator       & >( base_random_access_iterator::operator++( ) ); }
    ra_iterator   operator++(int) noexcept { return static_cast<ra_iterator const &&>( base_random_access_iterator::operator++(0) ); }
    ra_iterator & operator--(   ) noexcept { return static_cast<ra_iterator       & >( base_random_access_iterator::operator--( ) ); }
    ra_iterator   operator--(int) noexcept { return static_cast<ra_iterator const &&>( base_random_access_iterator::operator--(0) ); }

    friend constexpr bool operator==( ra_iterator const & left, ra_iterator const & right ) noexcept { return left.index_ == right.index_; }

    operator fwd_iterator() const noexcept { return static_cast<fwd_iterator const &>( static_cast<base_iterator const &>( *this ) ); }
}; // class ra_iterator

template <typename Key>
typename
bptree_base_wkey<Key>::iterator
bptree_base_wkey<Key>::erase( const_iterator const iter ) noexcept
{
    auto const [node, key_offset]{ iter.base().pos() };
    return static_cast<iterator &&>( make_iter( erase( leaf( node ), key_offset ) ) );
}

template <typename Key>
template <typename N> [[ gnu::sysv_abi ]]
void bptree_base_wkey<Key>::move_keys
(
    N const & source, node_size_type const src_begin, node_size_type const src_end,
    N       & target, node_size_type const tgt_begin
) noexcept
{
    BOOST_ASSUME( &source != &target ); // otherwise could require move_backwards or shift_*
    BOOST_ASSUME( src_begin <= src_end );
    BOOST_ASSUME( ( src_end - src_begin ) <= N::max_values );
    BOOST_ASSUME( tgt_begin < N::max_values );
    std::uninitialized_move( &source.keys[ src_begin ], &source.keys[ src_end ], &target.keys[ tgt_begin ] );
}
template <typename Key> [[ gnu::noinline, gnu::sysv_abi ]]
void bptree_base_wkey<Key>::move_chldrn
(
    inner_node const & source, node_size_type const src_begin, node_size_type const src_end,
    inner_node       & target, node_size_type const tgt_begin
) noexcept
{
    BOOST_ASSUME( &source != &target ); // otherwise could require move_backwards or shift_*
    BOOST_ASSUME( src_begin <= src_end );
    auto const count{ static_cast<node_size_type>( src_end - src_begin ) };
    BOOST_ASSUME( count     <= inner_node::min_children + 1 );
    BOOST_ASSUME( tgt_begin <  inner_node::max_children     );
    auto const src_chldrn{ &source.children[ src_begin ] };

    auto const target_slot{ slot_of( target ) };
    for ( node_size_type ch_idx{ 0 }; ch_idx < count; ++ch_idx )
    {
        auto & ch_slot{ src_chldrn[ ch_idx ] };
        auto & child  { node( ch_slot ) };
        target.children[ tgt_begin + ch_idx ] = std::move( ch_slot );
        child.parent                          = target_slot;
        child.parent_child_idx                = tgt_begin + ch_idx;
    }
}


////////////////////////////////////////////////////////////////////////////////
// \class bp_tree
////////////////////////////////////////////////////////////////////////////////

template <typename Key, typename Comparator = std::less<>>
class bp_tree
    :
    public  bptree_base_wkey<Key>,
    private Comparator,
    public  boost::stl_interfaces::sequence_container_interface<bp_tree<Key, Comparator>, boost::stl_interfaces::element_layout::discontiguous>
{
private:
    using base     = bptree_base_wkey<Key>;
    using stl_impl = boost::stl_interfaces::sequence_container_interface<bp_tree<Key, Comparator>, boost::stl_interfaces::element_layout::discontiguous>;

    using depth_t        = base::depth_t;
    using node_size_type = base::node_size_type;
    using key_const_arg  = base::key_const_arg;
    using node_slot      = base::node_slot;
    using node_header    = base::node_header;
    using leaf_node      = base::leaf_node;
    using root_node      = base::root_node;
    using inner_node     = base::inner_node;
    using parent_node    = base::parent_node;
    using fwd_iterator   = base::fwd_iterator;
    using  ra_iterator   = base:: ra_iterator;
    using iter_pos       = base::iter_pos;

    using bptree_base::as;
    using bptree_base::can_borrow;
    using bptree_base::children;
    using bptree_base::keys;
    using bptree_base::node;
    using bptree_base::num_chldrn;
    using bptree_base::lshift_keys;
    using bptree_base::rshift_keys;
    using bptree_base::lshift_chldrn;
    using bptree_base::rshift_chldrn;
    using bptree_base::slot_of;
    using bptree_base::underflowed;
    using bptree_base::verify;

    using base::free;
    using base::insrt_child;
    using base::leaf;
    using base::leaf_level;
    using base::parent;
    using base::right;
    using base::root;

public:
    static constexpr auto unique{ true }; // TODO non unique
    static constexpr auto transparent_comparator{ requires{ typename Comparator::is_transparent; } };

    using size_type       = base::size_type;
    using value_type      = base::value_type;
    using       pointer   = value_type       *;
    using const_pointer   = value_type const *;
    using       reference = value_type       &;
    using const_reference = value_type const &;
    using       iterator  = base::      iterator;
    using const_iterator  = base::const_iterator;

    using base::empty;
    using base::size;
    using base::clear;

    bp_tree() noexcept = default;
    bp_tree( Comparator const & comp ) noexcept : Comparator{ comp } {}

    static constexpr base::size_type max_size() noexcept
    {
        auto const max_number_of_nodes     { std::numeric_limits<typename node_slot::value_type>::max() };
        auto const max_number_of_leaf_nodes{ /*TODO*/ max_number_of_nodes };
        auto const values_per_node         { leaf_node::max_values };
        auto const max_sz                  { max_number_of_leaf_nodes * values_per_node };
        static_assert( max_sz <= std::numeric_limits<typename base::size_type>::max() );
        return max_sz;
    }

    [[ gnu::pure ]] iterator begin() noexcept { return static_cast<iterator &&>( base::begin() ); } using stl_impl::begin;
    [[ gnu::pure ]] iterator   end() noexcept { return static_cast<iterator &&>( base::  end() ); } using stl_impl::end  ;

    [[ gnu::pure ]]                           ra_iterator  ra_begin()       noexcept { return static_cast<ra_iterator &&>( base::ra_begin() ); }
    [[ gnu::pure ]]                           ra_iterator  ra_end  ()       noexcept { return static_cast<ra_iterator &&>( base::ra_end  () ); }
    [[ gnu::pure ]] std::basic_const_iterator<ra_iterator> ra_begin() const noexcept { return const_cast<bp_tree &>( *this ).ra_begin(); }
    [[ gnu::pure ]] std::basic_const_iterator<ra_iterator> ra_end  () const noexcept { return const_cast<bp_tree &>( *this ).ra_end  (); }

    auto random_access()       noexcept { return std::ranges::subrange{ ra_begin(), ra_end(), size() }; }
    auto random_access() const noexcept { return std::ranges::subrange{ ra_begin(), ra_end(), size() }; }

    [[ nodiscard ]] bool           contains   ( KeyType<transparent_comparator, Key> auto const & key ) const noexcept { return contains_impl   ( pass_in_reg{ key } ); }
    [[ nodiscard ]]       iterator find       ( KeyType<transparent_comparator, Key> auto const & key )       noexcept { return find_impl       ( pass_in_reg{ key } ); }
    [[ nodiscard ]] const_iterator find       ( KeyType<transparent_comparator, Key> auto const & key ) const noexcept { return const_cast<bp_tree &>( *this ).find( key ); }
    [[ nodiscard ]]       iterator lower_bound( KeyType<transparent_comparator, Key> auto const & key )       noexcept { return lower_bound_impl( pass_in_reg{ key } ); }
    [[ nodiscard ]] const_iterator lower_bound( KeyType<transparent_comparator, Key> auto const & key ) const noexcept { return const_cast<bp_tree &>( *this ).lower_bound( key ); }

    std::pair<iterator, bool> insert( KeyType<transparent_comparator, Key> auto const & key ) { return insert_impl( pass_in_reg{ key } ); }

    iterator insert( const_iterator const pos_hint, KeyType<transparent_comparator, Key> auto const & key ) { return insert_impl( pos_hint, pass_in_reg{ key } ); }

    // bulk insert
    // performance note: insertion of existing values into a unique bp_tree is
    // supported and accounted for (the input values are skipped) but it is
    // considered an 'unlikely' event and as such it is handled by sad/cold paths
    // TODO complete std insert interface (w/ ranges, iterators, hints...)
    template <std::input_iterator InIter>
    size_type insert( InIter const begin, InIter const end ) { return insert( base::bulk_insert_prepare( std::ranges::subrange( begin, end ) ) ); }
    size_type insert( std::ranges::range auto const & keys ) { return insert( base::bulk_insert_prepare( std::ranges::subrange( keys       ) ) ); }

    size_type merge( bp_tree && other );

    using base::erase;
    [[ nodiscard ]] BOOST_NOINLINE
    bool erase( key_const_arg key ) noexcept
    {
        auto const location{ find_nodes_for( key ) };

        if ( !location.leaf_offset.exact_find ) [[ unlikely ]]
            return false;

        leaf_node & leaf{ location.leaf };
        if ( this->hdr().depth_ != 1 )
        {
            verify( leaf );
            BOOST_ASSUME( leaf.num_vals >= leaf.min_values );
        }
        auto const leaf_key_offset{ location.leaf_offset.pos };
        if ( location.inner ) [[ unlikely ]] // "most keys are in the leaf nodes"
        {
            BOOST_ASSUME( leaf_key_offset == 0 );
            BOOST_ASSUME( eq( leaf.keys[ leaf_key_offset ], key ) );

            auto & inner        { node<inner_node>( location.inner ) };
            auto & separator_key{ inner.keys[ location.inner_offset ] };
            BOOST_ASSUME( eq( separator_key, key ) );
            BOOST_ASSUME( leaf_key_offset + 1 < leaf.num_vals );
            separator_key = leaf.keys[ leaf_key_offset + 1 ];
        }

        base::erase( leaf, leaf_key_offset );
        return true;
    }

    void swap( bp_tree & other ) noexcept { base::swap( other ); }

    [[ nodiscard ]] Comparator const & comp() const noexcept { return *this; }

    // UB if the comparator is changed in such a way as to invalidate to order of elements already in the container
    [[ nodiscard ]] Comparator & mutable_comp() noexcept { return *this; }

private: // pass-in-reg public function overloads/impls
    bool contains_impl( Reg auto const key ) const noexcept { return !empty() && const_cast<bp_tree &>( *this ).find_nodes_for( key ).leaf_offset.exact_find; }

    [[ using gnu: noinline, pure, sysv_abi ]]
    iterator find_impl( Reg auto const key ) noexcept
    {
        if ( !empty() ) [[ likely ]]
        {
            auto const location{ find_nodes_for( key ) };
            if ( location.leaf_offset.exact_find ) [[ likely ]] {
                return iterator{ this->nodes_, { slot_of( location.leaf ), location.leaf_offset.pos } };
            }
        }

        return this->end();
    }

    iterator lower_bound_impl( Reg auto const key ) noexcept
    {
        if ( !empty() ) [[ likely ]]
        {
            auto const location{ find_nodes_for( key ) };
            return iterator{ this->nodes_, { slot_of( location.leaf ), location.leaf_offset.pos } };
        }

        return this->end();
    }

    std::pair<iterator, bool> insert_impl( Reg auto const v )
    {
        if ( empty() )
        {
            auto & root{ static_cast<leaf_node &>( base::create_root() ) };
            BOOST_ASSUME( root.num_vals == 1 );
            root.keys[ 0 ] = v;
            return { begin(), true };
        }

        auto const locations{ find_nodes_for( v ) };
        BOOST_ASSUME( !locations.inner );
        BOOST_ASSUME( !locations.inner_offset );
        if ( locations.leaf_offset.exact_find ) [[ unlikely ]]
            return { { this->nodes_, { slot_of( locations.leaf ), locations.leaf_offset.pos } }, false };

        auto const insert_pos_next{ base::insert( locations.leaf, locations.leaf_offset.pos, Key{ v }, { /*insertion starts from leaves which do not have children*/ } ) };
        ++this->hdr().size_;
        return { std::prev( iterator{ this->nodes_, { insert_pos_next.node, insert_pos_next.next_insert_offset } } ), true };
    }

    iterator insert_impl( const_iterator const pos_hint, Reg auto const v )
    {
        // yes, for starters generic 'hint as just a hint' is not supported
        BOOST_ASSUME( !empty() );
        BOOST_ASSERT_MSG( le( v, *pos_hint ), "Invalid insertion hint" );
        BOOST_ASSERT_MSG( !unique || ge( v, *std::prev( pos_hint ) ), "Invalid insertion hint" );

        auto const [hint_slot, hint_slot_offset]{ pos_hint.base().pos() };
        auto const insert_pos_next{ base::insert( leaf( hint_slot ), hint_slot_offset, Key{ v }, { /*insertion starts from leaves which do not have children*/ } ) };
        ++this->hdr().size_;
        return std::prev( iterator{ this->nodes_, { insert_pos_next.node, insert_pos_next.next_insert_offset } } );
    }

private:
    // lower_bound find
    struct find_pos // msvc pass-in-reg facepalm
    {
        node_size_type pos        : ( sizeof( node_size_type ) * CHAR_BIT - 1 );
        node_size_type exact_find : 1;
    };
    [[ using gnu: pure, hot, noinline, sysv_abi ]]
    find_pos find( Key const keys[], node_size_type const num_vals, Reg auto const value ) const noexcept
    {
        // TODO branchless binary search, Alexandrescu's ideas, https://orlp.net/blog/bitwise-binary-search ...
        BOOST_ASSUME( num_vals > 0 );
        auto const & __restrict comp{ this->comp() };
        node_size_type pos_idx;
        if constexpr ( use_linear_search_for_sorted_array<Comparator, Key>( 1, leaf_node::max_values ) )
        {
            auto k{ 0 };
            while ( ( k != num_vals ) && comp( keys[ k ], value ) )
                ++k;
            pos_idx = static_cast<node_size_type>( k );
        }
        else
        {
            auto const pos_iter{ std::lower_bound( &keys[ 0 ], &keys[ num_vals ], value, comp ) };
            pos_idx = static_cast<node_size_type>( std::distance( &keys[ 0 ], pos_iter ) );
        }
        auto const exact_find{ ( pos_idx != num_vals ) & !comp( value, keys[ std::min<node_size_type>( pos_idx, num_vals - 1 ) ] ) };
        return { pos_idx, reinterpret_cast<bool const &>( exact_find ) };
    }
    find_pos find( auto const & node, auto const & value ) const noexcept { return find( node.keys, node.num_vals, pass_in_reg{ value } ); }
    [[ using gnu: pure, hot, sysv_abi ]]
    find_pos find_with_offset( auto const & node, node_size_type const offset, Reg auto const value ) const noexcept
    {
        BOOST_ASSUME( offset < node.num_vals );
        auto result{ find( &node.keys[ offset ], node.num_vals - offset, value ) };
        result.pos += offset;
        return result;
    }

    struct key_locations
    {
        leaf_node & leaf;
        find_pos    leaf_offset;
        // optional - if also present in an inner node as a separator key
        node_size_type inner_offset; // ordered for compact layout
        node_slot      inner;
    };

    [[ using gnu: pure, hot, sysv_abi ]]
    key_locations find_nodes_for( Reg auto const key ) noexcept
    {
        node_slot      separator_key_node;
        node_size_type separator_key_offset{};
        // a leaf (lone) root is implicitly handled by the loop condition:
        // depth_ == 1 so the loop is skipped entirely and the lone root is
        // never examined through the incorrectly typed reference
        auto       p_node{ &bptree_base::as<parent_node>( root() ) };
        auto const depth { this->hdr().depth_ };
        BOOST_ASSUME( depth >= 1 );
        for ( auto level{ 0 }; level < depth - 1; ++level )
        {
            auto [pos, exact_find]{ find( *p_node, key ) };
            if ( exact_find )
            {
                // separator key - it also means we have to traverse to the right
                BOOST_ASSUME( !separator_key_node ); // exact_find may happen at most once
                separator_key_node   = slot_of( *p_node );
                separator_key_offset = pos;
                ++pos; // traverse to the right child
            }
            p_node = &node<parent_node>( p_node->children[ pos ] );
        }
        auto & leaf{ base::template as<leaf_node>( *p_node ) };
        return
        {
            leaf,
            separator_key_node ? find_pos{ 0, true } : find( leaf, key ),
            separator_key_offset,
            separator_key_node
        };
    }
    key_locations find_nodes_for( Key const & key ) noexcept { return find_nodes_for( pass_in_reg{ key } ); }

    auto find_next( leaf_node const & starting_leaf, node_size_type const starting_leaf_offset, Reg auto const key ) const noexcept
    {
        if ( leq( key, keys( starting_leaf ).back() ) )
        {
            auto const pos{ find_with_offset( starting_leaf, starting_leaf_offset, key ) };
            BOOST_ASSUME( pos.pos != starting_leaf.num_vals );
            BOOST_ASSUME( pos.pos >= starting_leaf_offset   );
            return std::make_pair( const_cast<leaf_node *>( &starting_leaf ), pos );
        }

        if ( !starting_leaf.right ) [[ unlikely ]] // we are at the end of the tree/leaf level: key not present at all
            return std::make_pair( const_cast<leaf_node *>( &starting_leaf ), find_pos{ starting_leaf.num_vals, false } );

        // key in tree but not in starting leaf: go up the tree
        auto const * prnt{ &parent( starting_leaf ) };
        auto         parent_offset{ starting_leaf.parent_child_idx };
        BOOST_ASSUME( ( parent_offset == prnt->num_vals ) || ge( prnt->keys[ parent_offset ], starting_leaf.keys[ 0 ] ) );
        auto const depth{ this->hdr().depth_ }; BOOST_ASSUME( depth >= 1 );
        auto       level{ depth - 1 };
        while ( le( keys( *prnt ).back(), key ) )
        {
            if ( level == 1 ) [[ unlikely ]]
            {
                // reached the root
                BOOST_ASSUME( !prnt->parent );
                // the case where the key does not exist at all is handled at
                // the beginning so the only case where parent_offset could
                // point to the end is on intermediate inner/parent nodes (when
                // depth is more then 2 levels)
                // (and this has to be handled because otherwise the
                // find_with_offset call below would get fed empty input which
                // it does not support)
                BOOST_ASSUME( depth > 2 || ( parent_offset < prnt->num_vals ) );
                parent_offset = std::min( parent_offset, node_size_type( prnt->num_vals - 1 ) );
                break;
            }
            parent_offset = prnt->parent_child_idx;
            prnt          = &parent( *prnt );
            --level;
        }
        BOOST_ASSUME( parent_offset < prnt->num_vals );
        // descend to the leaf containing the key
        for ( ; level < depth; ++level )
        {
            auto [pos, exact_find]{ find_with_offset( *prnt, parent_offset, key ) };
            BOOST_ASSERT( !exact_find );
            pos += exact_find; // traverse to the right child for separator keys
            prnt = &node<inner_node>( children( *prnt )[ pos ] );
            parent_offset = 0;
        }
        BOOST_ASSUME( parent_offset == 0 );
        auto const & containing_leaf{ as<leaf_node>( *prnt ) };
        auto const pos{ find( containing_leaf, key ) };
        BOOST_ASSUME
        (
            ( &starting_leaf != &containing_leaf  ) ||
            // the worst case: when the value falls between existing nodes we
            // will land on the starting node again - TODO insert a new node
            ( pos.pos == containing_leaf.num_vals )
        );
        return std::make_pair( const_cast<leaf_node *>( &containing_leaf ), pos );
    }

    template <typename N>
    void insert( N & target_node, key_const_arg v, node_slot const right_child )
    {
        auto const pos{ find( target_node, v ).pos };
        base::insert( target_node, pos, v, right_child );
    }

    size_type insert( typename base::bulk_copied_input );

    // bulk insert helper: merge a new, presorted leaf into an existing leaf
    auto merge
    (
        leaf_node const & source, node_size_type source_offset,
        leaf_node       & target, node_size_type target_offset
    ) noexcept;

    node_size_type merge_interleaved_values
    (
        Key const source0[], node_size_type const source0_size,
        Key const source1[], node_size_type const source1_size,
        Key       target []
    ) const noexcept
    {
        node_size_type const input_size( source0_size + source1_size );
        if constexpr ( unique )
        {
            auto const out_pos{ std::set_union( source0, &source0[ source0_size ], source1, &source1[ source1_size ], target, comp() ) };
            auto const merged_size{ static_cast<node_size_type>( out_pos - target ) };
            BOOST_ASSUME( merged_size <= input_size );
            return merged_size;
        }
        else
        {
            auto const out_pos{ std::merge( source0, &source0[ source0_size ], source1, &source1[ source1_size ], target, comp() ) };
            BOOST_ASSUME( out_pos = target + input_size );
            return input_size;
        }
    }

#if !( defined( _MSC_VER ) && !defined( __clang__ ) )
    // ambiguous call w/ VS 17.11.5
    void verify( auto const & node ) const noexcept
    {
        BOOST_ASSERT( std::ranges::is_sorted( keys( node ), comp() ) );
        base::verify( node );
    }
#endif

    [[ gnu::pure ]] bool le( key_const_arg left, key_const_arg right ) const noexcept { return comp()( left, right ); }
    [[ gnu::pure ]] bool ge( key_const_arg left, key_const_arg right ) const noexcept { return comp()( right, left ); }
    [[ gnu::pure ]] bool eq( key_const_arg left, key_const_arg right ) const noexcept
    {
        if constexpr ( requires{ comp().eq( left, right ); } )
            return comp().eq( left, right );
        if constexpr ( detail::is_simple_comparator<Comparator> && requires{ left == right; } )
            return left == right;
        return !comp()( left, right ) && !comp()( right, left );
    }
    [[ gnu::pure ]] bool leq( key_const_arg left, key_const_arg right ) const noexcept
    {
        if constexpr ( requires{ comp().leq( left, right ); } )
            return comp().leq( left, right );
        return !comp()( right, left );
    }
    [[ gnu::pure ]] bool geq( key_const_arg left, key_const_arg right ) const noexcept
    {
        if constexpr ( requires{ comp().geq( left, right ); } )
            return comp().geq( left, right );
        return !comp()( left, right );
    }
}; // class bp_tree

PSI_WARNING_DISABLE_POP()


template <typename Key, typename Comparator>
// bulk insert helper: merge a new, presorted leaf into an existing leaf
auto bp_tree<Key, Comparator>::merge
(
    leaf_node const & source, node_size_type const source_offset,
    leaf_node       & target, node_size_type const target_offset
) /*?*/noexcept
{
    verify( source );
    verify( target );
    BOOST_ASSUME( source_offset < source.num_vals );
    node_size_type       input_length   ( source.num_vals   - source_offset   );
    node_size_type const available_space( target.max_values - target.num_vals );
    auto   const src_keys{ &source.keys[ source_offset ] };
    auto &       tgt_keys{ target.keys };
    if ( target_offset == 0 ) [[ unlikely ]]
    {
        auto const & new_separator{ src_keys[ 0 ] };
        BOOST_ASSUME( new_separator < tgt_keys[ 0 ] );
        base::update_separator( target, new_separator );
        // TODO rather simply insert the source leaf into the parent (if all of
        // its keys come before the first key in target)
    }
    if ( !available_space ) [[ unlikely ]]
    {
        // support merging nodes from another tree instance
        auto const source_slot{ base::is_my_node( source ) ? slot_of( source ) : node_slot{} };
        BOOST_ASSERT( find( target, src_keys[ 0 ] ).pos == target_offset );
        if ( eq( target.keys[ target_offset ], src_keys[ 0 ] ) ) [[ unlikely ]]
        {
            return std::make_tuple<node_size_type, node_size_type>( 0, 1, &target, target_offset );
        }
        auto       [target_slot, next_tgt_offset]{ base::split_to_insert( target, target_offset, pass_rv_in_reg{ /*mrmlj*/Key{ src_keys[ 0 ] } }, {} ) };
        auto const & src{ source_slot ? leaf( source_slot ) : source };
        auto       & tgt{               leaf( target_slot )          };
        BOOST_ASSUME( next_tgt_offset <= tgt.num_vals );
        auto const next_src_offset{ static_cast<node_size_type>( source_offset + 1 ) };
        // next_tgt_offset returned by split_to_insert points to the
        // position in the target node that immediately follows the
        // position for the inserted src_keys[ 0 ] - IOW it need not be
        // the position for src.keys[ next_src_offset ]
        if ( next_tgt_offset != tgt.num_vals ) // necessary check because find assumes non-empty input
        {
            next_tgt_offset = find_with_offset( tgt, next_tgt_offset, pass_in_reg{ src.keys[ next_src_offset ] } ).pos;
        }
        return std::make_tuple<node_size_type, node_size_type>( 1, 1, &tgt, next_tgt_offset );
    }

    auto copy_size{ std::min( input_length, available_space ) };
    // If there is an existing right sibling we must first check if the
    // source contains values beyond its separator key (and adjust the copy
    // size accordingly to maintain the sorted property).
    if ( target.right )
    {
        auto const & right_delimiter    { right( target ).keys[ 0 ] };
        auto const   less_than_right_pos{ find( src_keys, copy_size, pass_in_reg{ right_delimiter } ) };
        BOOST_ASSUME( !less_than_right_pos.exact_find );
        if ( less_than_right_pos.pos != copy_size )
        {
            BOOST_ASSUME( less_than_right_pos.pos < copy_size );
            node_size_type const input_end_for_target( less_than_right_pos.pos + source_offset );
            BOOST_ASSUME( input_end_for_target >  source_offset   );
            BOOST_ASSUME( input_end_for_target <= source.num_vals );
            copy_size = static_cast<node_size_type>( input_end_for_target - source_offset );
        }
    }

    auto & tgt_size{ target.num_vals };
    node_size_type inserted_size;
    node_size_type next_tgt_offset;
    if ( target_offset == tgt_size ) // a simple append
    {
        std::copy_n( src_keys, copy_size, &tgt_keys[ target_offset ] );
        tgt_size        += copy_size;
        inserted_size    = copy_size;
        next_tgt_offset  = tgt_size;
    }
    else
    {
        BOOST_ASSUME( copy_size + tgt_size <= leaf_node::max_values );
        // make room for merge: move existing values (beyond the insertion/merge
        // point) to the end of the buffer
        std::move_backward( &tgt_keys[ target_offset ], &tgt_keys[ tgt_size ], &tgt_keys[ tgt_size + copy_size ] );
        auto const new_tgt_size{ target_offset + merge_interleaved_values
        (
            &src_keys[ 0                         ], copy_size,
            &tgt_keys[ target_offset + copy_size ], tgt_size - target_offset,
            &tgt_keys[ target_offset             ]
        ) };
        inserted_size   = static_cast<node_size_type>( new_tgt_size - tgt_size );
        tgt_size        = static_cast<node_size_type>( new_tgt_size            );
        next_tgt_offset = target_offset + 1;
    }
    verify( target );
    BOOST_ASSUME( inserted_size <= copy_size );
    return std::make_tuple( inserted_size, copy_size, &target, next_tgt_offset );
}


template <typename Key, typename Comparator>
bp_tree<Key, Comparator>::size_type
bp_tree<Key, Comparator>::insert( typename base::bulk_copied_input const input )
{
    // https://www.sciencedirect.com/science/article/abs/pii/S0020025502002025 On batch-constructing B+-trees: algorithm and its performance
    // https://www.vldb.org/conf/2001/P461.pdf An Evaluation of Generic Bulk Loading Techniques
    // https://stackoverflow.com/questions/15996319/is-there-any-algorithm-for-bulk-loading-in-b-tree

    auto const [begin_leaf, end_pos, total_size]{ input };
    ra_iterator const p_new_nodes_begin{ *this, { begin_leaf, 0 }, 0          };
    ra_iterator const p_new_nodes_end  { *this, end_pos          , total_size };
    std::sort( p_new_nodes_begin, p_new_nodes_end, comp() );

    if ( empty() )
    {
        base::bulk_insert_into_empty( begin_leaf, end_pos, total_size );
        return total_size;
    }

    auto p_new_keys{ p_new_nodes_begin };
    auto [source_slot, source_slot_offset]{ p_new_keys.pos() };
    auto src_leaf{ &leaf( source_slot ) };

    auto const start_pos{ find_nodes_for( *p_new_keys ) };
    auto       tgt_leaf         { &start_pos.leaf };
    auto       tgt_leaf_next_pos{ start_pos.leaf_offset };

    size_type inserted{ 0 };
    do
    {
        if ( unique && tgt_leaf_next_pos.exact_find ) [[ unlikely ]]
        {
            ++p_new_keys;
            continue;
        }

        BOOST_ASSUME( source_slot_offset < src_leaf->num_vals );
        // if we have reached the end of the rightmost leaf simply perform
        // a bulk_append
        if ( ( tgt_leaf_next_pos.pos == tgt_leaf->num_vals ) && !tgt_leaf->right )
        {
            auto const so_far_consumed{ static_cast<size_type>( p_new_keys - p_new_nodes_begin ) };
            BOOST_ASSUME( so_far_consumed < total_size );
            std::shift_left( &src_leaf->keys[ 0 ], &src_leaf->keys[ src_leaf->num_vals ], source_slot_offset );
            src_leaf->num_vals -= source_slot_offset;
            base::link( *tgt_leaf, *src_leaf );
            base::bulk_append_fill_incomplete_leaf( *src_leaf ); // yes, in case src_leaf is really incomplete, the shift_left above is redundant
            auto const rightmost_parent_slot{ tgt_leaf->parent };
            auto const parent_pos           { tgt_leaf->parent_child_idx }; // key idx = child idx - 1 & this is the 'next' key
            base::bulk_append( src_leaf, { rightmost_parent_slot, parent_pos } );
            inserted += static_cast<size_type>( total_size - so_far_consumed );
            break;
        }
        // TODO in-the-middle partial bulk-inserts

        auto const [inserted_count, consumed_source, tgt_next_leaf, tgt_next_offset]
        {
            merge
            (
                *src_leaf, source_slot_offset,
                *tgt_leaf, tgt_leaf_next_pos.pos
            )
        };
        tgt_leaf = tgt_next_leaf;

        // merge might have caused a relocation (by calling split_to_insert)
        // TODO use iter_pos directly
        p_new_keys     .update_pool_ptr( this->nodes_ );
        p_new_nodes_end.update_pool_ptr( this->nodes_ );
        src_leaf = &leaf( source_slot );

        p_new_keys += consumed_source;
        inserted   += inserted_count;

        if ( source_slot != p_new_keys.pos().node ) // have we moved to the next node?
        {
            // merged leaves (their contents) were effectively copied into
            // existing leaves (instead of simply linked into the tree
            // structure) and now have to be returned to the free pool
            // TODO: add leak detection to/for the entire bp_tree class
            base::unlink_right( *src_leaf );
            free( *src_leaf );

            source_slot = p_new_keys.pos().node;
            src_leaf    = &leaf( source_slot );
        }
        source_slot_offset = p_new_keys.pos().value_offset;

        // seek the next position starting from the current one (relying on the
        // that we are using presorted data) rather than starting everytime from
        // scratch (using find_nodes_for)
        std::tie( tgt_leaf, tgt_leaf_next_pos ) =
            find_next( *tgt_leaf, tgt_next_offset, pass_in_reg{ src_leaf->keys[ source_slot_offset ] } );
    } while ( p_new_keys != p_new_nodes_end );

    BOOST_ASSUME( inserted <= total_size );
    this->hdr().size_ += inserted;
    return inserted;
} // bp_tree::insert()

template <typename Key, typename Comparator>
bp_tree<Key, Comparator>::size_type
bp_tree<Key, Comparator>::merge( bp_tree && other )
{
    // This function follows nearly the same logic as bulk insert (consult it
    // for more comments), the main differences being:
    //  - no need to copy and sort the input
    //  - the bulk_append phase has to first copy the remainder of the source
    //    nodes (they are not somehow 'extractable' from the source tree)
    //  - care has to be taken around the fact that source leaves are coming
    //    from a different tree instance (i.e. from a different container) e.g.
    //    when resolving slots to node references.
    // TODO further deduplicate with insert
    if ( empty() ) {
        swap( other );
        return size();
    }

    auto const total_size{ other.size() };
    this->reserve_additional( total_size );

    auto const p_new_nodes_begin{ other.ra_begin() };
    auto const p_new_nodes_end  { other.ra_end  () };

    auto p_new_keys{ p_new_nodes_begin };
    auto src_leaf          { &other.leaf( p_new_keys.pos().node ) };
    auto source_slot_offset{ p_new_keys.pos().value_offset };

    auto const start_pos{ find_nodes_for( *p_new_keys ) };
    auto       tgt_leaf         { &start_pos.leaf };
    auto       tgt_leaf_next_pos{ start_pos.leaf_offset };

    size_type inserted{ 0 };
    do
    {
        if ( unique && tgt_leaf_next_pos.exact_find ) [[ unlikely ]]
        {
            ++p_new_keys;
            continue;
        }

        BOOST_ASSUME( source_slot_offset < src_leaf->num_vals );
        // simple bulk_append at the end of the rightmost leaf
        if ( ( tgt_leaf_next_pos.pos == tgt_leaf->num_vals ) && !tgt_leaf->right )
        {
            // pre-copy the (remainder of the) source into fresh nodes in
            // order to simply call bulk_append
            node_slot src_copy_begin;
            node_slot prev_src_copy_node;
            for ( ;; )
            {
                auto & src_leaf_copy{ this->template new_node<leaf_node>() };
                if ( !src_copy_begin )
                {
                    src_copy_begin = slot_of( src_leaf_copy );
                    this->move_keys( *src_leaf, source_slot_offset, src_leaf->num_vals, src_leaf_copy, 0 );
                    src_leaf_copy.num_vals = src_leaf->num_vals - source_slot_offset;
                    src_leaf->num_vals     = source_slot_offset;
                    this->link( *tgt_leaf, src_leaf_copy );
                    this->bulk_append_fill_incomplete_leaf( src_leaf_copy );
                }
                else
                {
                    this->move_keys( *src_leaf, 0, src_leaf->num_vals, src_leaf_copy, 0 );
                    src_leaf_copy.num_vals = src_leaf->num_vals;
                    src_leaf->num_vals     = 0;
                    this->link( this->leaf( prev_src_copy_node ), src_leaf_copy );
                }
                BOOST_ASSUME( !src_leaf_copy.parent );
                BOOST_ASSUME( !src_leaf_copy.parent_child_idx );
                    
                if ( !src_leaf->right )
                    break;
                src_leaf           = &other.right( *src_leaf );
                prev_src_copy_node = slot_of( src_leaf_copy );
            }

            auto const so_far_consumed{ static_cast<size_type>( p_new_keys - p_new_nodes_begin ) };
            BOOST_ASSUME( so_far_consumed < total_size );
            this->bulk_append( &this->leaf( src_copy_begin ), { tgt_leaf->parent, tgt_leaf->parent_child_idx } );
            inserted += static_cast<size_type>( total_size - so_far_consumed );
            break;
        }
        // TODO in-the-middle partial bulk-inserts

        auto const [inserted_count, consumed_source, tgt_next_leaf, tgt_next_offset]
        {
            merge
            (
                *src_leaf, source_slot_offset,
                *tgt_leaf, tgt_leaf_next_pos.pos
            )
        };
        tgt_leaf = tgt_next_leaf;

        p_new_keys += consumed_source;
        inserted   += inserted_count;

        src_leaf           = &other.leaf( p_new_keys.pos().node );
        source_slot_offset = p_new_keys.pos().value_offset;

        std::tie( tgt_leaf, tgt_leaf_next_pos ) =
            find_next( *tgt_leaf, tgt_next_offset, pass_in_reg{ src_leaf->keys[ source_slot_offset ] } );
    } while ( p_new_keys != p_new_nodes_end );

    BOOST_ASSUME( inserted <= total_size );
    this->hdr().size_ += inserted;
    return inserted;
} // bp_tree::merge()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
