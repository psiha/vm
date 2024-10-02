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


template <typename Comparator, typename Key>
constexpr bool use_linear_search_for_sorted_array( [[ maybe_unused ]] std::uint32_t const minimum_array_length, std::uint32_t const maximum_array_length ) noexcept
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

    [[ gnu::pure ]] auto empty() const noexcept { return size() == 0; }

    void clear() noexcept;

    bptree_base( header_info hdr_info = {} ) noexcept;

    std::span<std::byte> user_header_data() noexcept;

    bool has_attached_storage() const noexcept { return nodes_.has_attached_storage(); }

    storage_result map_file  ( auto          const file, flags::named_object_construction_policy const policy ) noexcept { return init_header( nodes_.map_file  ( file, policy                        ) ); }
    storage_result map_memory( std::uint32_t const initial_capacity_as_number_of_nodes = 0                    ) noexcept { return init_header( nodes_.map_memory( initial_capacity_as_number_of_nodes ) ); }

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
        [[ gnu::pure ]] value_type operator*() const noexcept { return index; }
        [[ gnu::pure ]] bool operator==( node_slot const other ) const noexcept { return this->index == other.index; }
        [[ gnu::pure ]] explicit operator bool() const noexcept { return index != null.index; }
    }; // struct node_slot

    struct [[ nodiscard ]] node_header
    {
        using size_type = std::uint16_t;

        // At minimum we need single-linked/directed list in the vertical/depth and horizontal/breadth directions
        // (and the latter only for the leaf level - to have a connected sorted 'list' of all the values).
        // However having a precise vertical back/up-link (parent_child_idx):
        // * speeds up walks up the tree (as the parent (separator) key slots do not have to be searched for)
        // * simplifies code (enabling several functions to become independent of the comparator - no longer
        //   need searching - and moved up into the base bptree classes)
        // * while at the same time being a negligible overhead considering we are targeting much larger (page
        //   size sized) nodes.
        node_slot parent          {};
        node_slot left            {};
        node_slot right           {};
        size_type num_vals        {};
        size_type parent_child_idx{};
      /* TODO
        size_type start; // make keys and children arrays function as devectors: allow empty space at the beginning to avoid moves for small borrowings
      */

        [[ gnu::pure ]] bool is_root() const noexcept { return !parent; }

        // merely to prevent slicing (in return-node-by-ref cases)
        node_header( node_header const & ) = delete;
        constexpr node_header( node_header && ) noexcept = default;
        constexpr node_header(                ) noexcept = default;
        constexpr node_header & operator=( node_header && ) noexcept = default;
    }; // struct node_header
    using node_size_type = node_header::size_type;

    struct alignas( node_size ) node_placeholder : node_header {};
    struct alignas( node_size ) free_node        : node_header {};

    // SCARY iterator parts
    class base_iterator;
    class base_random_access_iterator;

    struct header
    {
        node_slot root_;
        node_slot free_list_;
        node_slot leaves_;
        size_t    size_;
        depth_t   depth_;
    }; // struct header

    using node_pool = vm::vector<node_placeholder, node_slot::value_type, false>;

protected:
    void swap( bptree_base & other ) noexcept;

    [[ gnu::pure ]] size_type size() const noexcept { return hdr().size_; }

    [[ gnu::cold ]] node_header & create_root();

    [[ gnu::pure ]] static bool underflowed( auto const & node ) noexcept { return node.num_vals < node.min_values; }
    [[ gnu::pure ]] static bool can_borrow ( auto const & node ) noexcept { return node.num_vals > node.min_values; }

    [[ gnu::pure ]] depth_t    leaf_level(                     ) const noexcept;
    [[ gnu::pure ]] bool    is_leaf_level( depth_t const level ) const noexcept;

    void free( node_header & ) noexcept;

    void reserve( node_slot::value_type additional_nodes );

    [[ gnu::pure ]] header       & hdr()       noexcept;
    [[ gnu::pure ]] header const & hdr() const noexcept { return const_cast<bptree_base &>( *this ).hdr(); }

    node_slot first_leaf() const noexcept { return hdr().leaves_; }

    static void verify( auto const & node ) noexcept
    {
        BOOST_ASSERT( std::ranges::is_sorted( keys( node ) ) );
        BOOST_ASSUME( node.num_vals <= node.max_values );
        // also used for underflowing nodes and (most problematically) for root nodes 'interpreted' as inner nodes...TODO...
        //BOOST_ASSUME( node.num_vals >= node.min_values );
    }

    static constexpr auto keys    ( auto       & node ) noexcept {                                             return std::span{ node.keys    , node.num_vals      }; }
    static constexpr auto keys    ( auto const & node ) noexcept {                                             return std::span{ node.keys    , node.num_vals      }; }
    static constexpr auto children( auto       & node ) noexcept { if constexpr ( requires{ node.children; } ) return std::span{ node.children, node.num_vals + 1U }; else return std::array<node_slot, 0>{}; }
    static constexpr auto children( auto const & node ) noexcept { if constexpr ( requires{ node.children; } ) return std::span{ node.children, node.num_vals + 1U }; else return std::array<node_slot, 0>{}; }

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

    [[ gnu::sysv_abi ]]
    std::pair<node_slot, node_slot> new_spillover_node_for( node_header & existing_node );

    node_placeholder & new_root( node_slot left_child, node_slot right_child );

    template <typename NodeType, typename SourceNode>
    static NodeType & as( SourceNode & slot ) noexcept
    {
        static_assert( sizeof( NodeType ) == sizeof( slot ) );
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

    [[ gnu::pure ]] node_slot slot_of( node_header const & ) const noexcept;

    static bool full( auto const & node ) noexcept { 
        BOOST_ASSUME( node.num_vals <= node.max_values );
        return node.num_vals == node.max_values;
    }

    [[ nodiscard ]] node_placeholder & new_node();

    template <typename N>
    [[ nodiscard ]] N & new_node() { return as<N>( new_node() ); }

private:
    auto header_data() noexcept { return detail::header_data<header>( nodes_.user_header_data() ); }

    storage_result init_header( storage_result ) noexcept;

protected:
    node_pool nodes_;
}; // class bptree_base

inline constexpr bptree_base::node_slot const bptree_base::node_slot::null{ static_cast<value_type>( -1 ) };


////////////////////////////////////////////////////////////////////////////////
// \class bptree_base::base_iterator
////////////////////////////////////////////////////////////////////////////////

class bptree_base::base_iterator
{
protected:
#ifndef NDEBUG // for bounds checking
    std::span<node_placeholder>
#else
    node_placeholder * __restrict
#endif
                   nodes_       {};
    node_slot      node_slot_   {};
    node_size_type value_offset_{};

    template <typename T, typename Comparator> friend class bp_tree;

    base_iterator( node_pool &, node_slot, node_size_type value_offset ) noexcept;

    [[ gnu::pure ]] node_header & node() const noexcept;

public:
    constexpr base_iterator() noexcept = default;

    base_iterator & operator++() noexcept;

    bool operator==( base_iterator const & ) const noexcept;
}; // class base_iterator

////////////////////////////////////////////////////////////////////////////////
// \class bptree_base::base_random_access_iterator
////////////////////////////////////////////////////////////////////////////////

class bptree_base::base_random_access_iterator : public base_iterator
{
protected:
    size_type index_;

    template <typename T, typename Comparator> friend class bp_tree;

    base_random_access_iterator( bptree_base & parent, node_slot const start_leaf, size_type const start_index ) noexcept
        : base_iterator{ parent.nodes_, start_leaf, 0 }, index_{ start_index } {}

public:
    constexpr base_random_access_iterator() noexcept = default;

    difference_type operator-( base_random_access_iterator const & other ) const noexcept { return static_cast<difference_type>( this->index_ - other.index_ ); }

    base_random_access_iterator & operator+=( difference_type n ) noexcept;

    base_random_access_iterator & operator++(   ) noexcept { base_iterator::operator++(); ++index_; return *this; }
    base_random_access_iterator   operator++(int) noexcept { auto current{ *this }; operator++(); return current; }

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
public:
    using key_type      = Key;
    using value_type    = key_type; // TODO map support

    using key_const_arg = std::conditional_t<std::is_trivial_v<Key>, Key, Key const &>;

public:
    void reserve( size_type additional_values )
    {
        additional_values = additional_values * 3 / 2; // TODO find the appropriate formula
        bptree_base::reserve( static_cast<node_slot::value_type>( ( additional_values + leaf_node::max_values - 1 ) / leaf_node::max_values ) );
    }

    // solely a debugging helper (include b+tree_print.hpp)
    void print() const;

protected: // node types
    struct alignas( node_size ) parent_node : node_header
    {
        static auto constexpr storage_space{ node_size - psi::vm::align_up( sizeof( node_header ), alignof( Key ) ) };

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

        static node_size_type constexpr storage_space{ node_size - psi::vm::align_up( sizeof( node_header ), alignof( Key ) ) };
        static node_size_type constexpr max_values   { storage_space / sizeof( Key ) };
        static node_size_type constexpr min_values   { ihalf_ceil<max_values> };

        Key keys[ max_values ];
    }; // struct leaf_node

    static_assert( sizeof( inner_node ) == node_size );
    static_assert( sizeof(  leaf_node ) == node_size );

protected: // iterators
    template <typename Impl, typename Tag>
    using iter_impl = boost::stl_interfaces::iterator_interface
    <
#   if !BOOST_STL_INTERFACES_USE_DEDUCED_THIS
        Impl,
#   endif
        Tag,
        Key
    >;

    class fwd_iterator;
    class  ra_iterator;

protected: // split_to_insert and its helpers
    template <typename N>
    void split_to_insert( N & node_to_split, node_size_type const insert_pos, key_const_arg value, node_slot const key_right_child )
    {
        auto [ node_slot, new_slot ]{ bptree_base::new_spillover_node_for( node_to_split ) };
        auto const max{ N::max_values };
        auto const mid{ N::min_values };
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
        auto key_to_propagate{ insertion_into_new_node // we cannot save a reference here because it might get invalidated by the new_node<root_node>() call below
            ? insert_into_new_node     ( *p_node, *p_new_node, value, insert_pos, static_cast<node_size_type>( new_insert_pos ), key_right_child )
            : insert_into_existing_node( *p_node, *p_new_node, value, insert_pos,                                                key_right_child )
        };

        verify( *p_node     );
        verify( *p_new_node );
        BOOST_ASSUME( p_node->num_vals == mid );

        // propagate the mid key to the parent
        if ( p_node->is_root() ) [[ unlikely ]] {
            new_root( node_slot, new_slot, std::move( key_to_propagate ) );
        } else {
            auto const key_pos{ static_cast<node_size_type>( p_new_node->parent_child_idx /*it is the _right_ child*/ - 1 ) };
            return insert( node<inner_node>( p_node->parent ), key_pos, std::move( key_to_propagate ), new_slot );
        }
    }

    void new_root( node_slot const left_child, node_slot const right_child, key_const_arg separator_key )
    {
        auto & new_root_node{ as<root_node>( bptree_base::new_root( left_child, right_child ) ) };
        new_root_node.keys    [ 0 ] = std::move( separator_key );
        new_root_node.children[ 0 ] = left_child;
        new_root_node.children[ 1 ] = right_child;
    }

    value_type insert_into_new_node
    (
        inner_node & node, inner_node & new_node,
        key_const_arg value,
        node_size_type const insert_pos, node_size_type const new_insert_pos,
        node_slot const key_right_child
    ) noexcept {
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

            keys( new_node )[ new_insert_pos - 1 ] = value;
        }
        insrt_child( new_node, new_insert_pos, key_right_child );

        node.num_vals = mid;

        BOOST_ASSUME( !underflowed( node     ) );
        BOOST_ASSUME( !underflowed( new_node ) );

        return key_to_propagate;
    }

    static value_type const & insert_into_new_node
    (
        leaf_node & node, leaf_node & new_node,
        key_const_arg value,
        node_size_type const insert_pos, node_size_type const new_insert_pos,
        node_slot const key_right_child
    ) noexcept {
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

        keys( new_node )[ new_insert_pos ] = value;
        auto const & key_to_propagate{ new_node.keys[ 0 ] };

        BOOST_ASSUME( !underflowed( node     ) );
        BOOST_ASSUME( !underflowed( new_node ) );

        return key_to_propagate;
    }

    value_type insert_into_existing_node( inner_node & node, inner_node & new_node, key_const_arg value, node_size_type const insert_pos, node_slot const key_right_child ) noexcept
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

        keys       ( node )[ insert_pos ] = value;
        insrt_child( node, insert_pos + 1, key_right_child );

        BOOST_ASSUME( !underflowed( node     ) );
        BOOST_ASSUME( !underflowed( new_node ) );

        return key_to_propagate;
    }

    static value_type const & insert_into_existing_node( leaf_node & node, leaf_node & new_node, key_const_arg value, node_size_type const insert_pos, node_slot const key_right_child ) noexcept
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

        keys( node )[ insert_pos ] = value;
        auto const & key_to_propagate{ new_node.keys[ 0 ] };

        BOOST_ASSUME( !underflowed( node     ) );
        BOOST_ASSUME( !underflowed( new_node ) );

        return key_to_propagate;
    }

protected: // 'other'
    template <typename N>
    void insert( N & target_node, node_size_type const target_node_pos, key_const_arg v, node_slot const right_child )
    {
        verify( target_node );
        if ( full( target_node ) ) [[ unlikely ]] {
            return split_to_insert( target_node, target_node_pos, v, right_child );
        } else {
            ++target_node.num_vals;
            rshift_keys( target_node, target_node_pos );
            target_node.keys[ target_node_pos ] = v;
            if constexpr ( requires { target_node.children; } ) {
                node_size_type const ch_pos( target_node_pos + /*>right< child*/ 1 );
                rshift_chldrn( target_node, ch_pos );
                this->insrt_child( target_node, ch_pos, right_child );
            }
        }
    }

    template <typename N>
    BOOST_NOINLINE
    void handle_underflow( N & node, depth_t const level )
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
        // (and so cannot be used to resolve siblings)
        auto const right_separator_key_idx{ static_cast<node_size_type>( parent_key_idx + parent_has_key_copy ) };
        auto const  left_separator_key_idx{ std::min( static_cast<node_size_type>( right_separator_key_idx - 1 ), parent.num_vals ) }; // (ab)use unsigned wraparound

        auto const has_right_sibling{ parent_child_idx < ( num_chldrn( parent ) - 1 ) };
        auto const has_left_sibling { parent_child_idx > 0 };
        auto const p_right_sibling{ has_right_sibling ? &this->node<N>( node.right ) : nullptr };
        auto const p_left_sibling { has_left_sibling  ? &this->node<N>( node.left  ) : nullptr };

        auto const p_right_separator_key{ has_right_sibling ? &parent.keys[ right_separator_key_idx ] : nullptr };
        auto const p_left_separator_key { has_left_sibling  ? &parent.keys[  left_separator_key_idx ] : nullptr };
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
            auto & __restrict depth_{ this->hdr().depth_ };
            auto & __restrict root_ { this->hdr().root_  };
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
    } // handle_underflow()

    root_node       & root()       noexcept { return as<root_node>( bptree_base::root() ); }
    root_node const & root() const noexcept { return const_cast<bptree_base_wkey &>( *this ).root(); }

    using bptree_base::free;
    void free( leaf_node & leaf ) noexcept {
        auto & first_leaf{ hdr().leaves_ };
        if ( first_leaf == slot_of( leaf ) ) {
            BOOST_ASSUME( !leaf.left );
            first_leaf = leaf.right;
            if ( first_leaf )
            {
                BOOST_ASSERT( node( first_leaf ).left );
                node( first_leaf ).left = {};
            }
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

        std::ranges::move( keys( right ), keys( left ).end() );
        left .num_vals += right.num_vals;
        right.num_vals  = 0;
        BOOST_ASSUME( left.num_vals >= 2 * min - 2 );
        BOOST_ASSUME( left.num_vals <= 2 * min - 1 );
        lshift_keys  ( parent, parent_key_idx   );
        lshift_chldrn( parent, parent_child_idx );
        BOOST_ASSUME( parent.num_vals );
        parent.num_vals--;

        unlink_node( right, left );

        right.right = {};
        verify( left  );
        free  ( right );
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
        unlink_node( right, left );

        move_chldrn( right, 0, num_chldrn( right ), left, num_chldrn( left ) );
        auto & separator_key{ parent.keys[ parent_key_idx ] };
        left.num_vals += 1;
        keys( left ).back() = std::move( separator_key );
        std::ranges::move( keys( right ), keys( left ).end() );

        left .num_vals += right.num_vals;
        right.num_vals  = 0;
        BOOST_ASSUME( left.num_vals >= left.max_values - 1 ); BOOST_ASSUME( left.num_vals <= left.max_values );
        verify( left  );
        free  ( right );

        lshift_keys  ( parent, parent_key_idx   );
        lshift_chldrn( parent, parent_child_idx );
        BOOST_ASSUME( parent.num_vals );
        parent.num_vals--;
    }
}; // class bptree_base_wkey

////////////////////////////////////////////////////////////////////////////////
// \class bptree_base_wkey::fwd_iterator
////////////////////////////////////////////////////////////////////////////////

template <typename Key>
class bptree_base_wkey<Key>::fwd_iterator
    :
    public base_iterator,
    public iter_impl<fwd_iterator, std::forward_iterator_tag>
{
private:
    using impl = iter_impl<fwd_iterator, std::forward_iterator_tag>;

    using base_iterator::base_iterator;

public:
    constexpr fwd_iterator() noexcept = default;

    Key & operator*() const noexcept { return static_cast<leaf_node &>( node() ).keys[ value_offset_ ]; }

    constexpr fwd_iterator & operator++() noexcept { return static_cast<fwd_iterator &>( base_iterator::operator++() ); }
    using impl::operator++;
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
private:
    using base_random_access_iterator::base_random_access_iterator;

public:
    constexpr ra_iterator() noexcept = default;

    Key & operator*() const noexcept { return static_cast<leaf_node &>( node() ).keys[ value_offset_ ]; }

    ra_iterator & operator+=( difference_type const n ) noexcept { return static_cast<ra_iterator &>( base_random_access_iterator::operator+=( n ) ); }

    ra_iterator & operator++(   ) noexcept { return static_cast<ra_iterator       & >( base_random_access_iterator::operator++( ) ); }
    ra_iterator   operator++(int) noexcept { return static_cast<ra_iterator const &&>( base_random_access_iterator::operator++(0) ); }

    friend constexpr bool operator==( ra_iterator const & left, ra_iterator const & right ) noexcept { return left.index_ == right.index_; }

    operator fwd_iterator() const noexcept { return static_cast<fwd_iterator const &>( static_cast<base_iterator const &>( *this ) ); }
}; // class ra_iterator

template <typename Key>
template <typename N> [[ gnu::noinline, gnu::sysv_abi ]]
void bptree_base_wkey<Key>::move_keys
(
    N const & source, node_size_type const src_begin, node_size_type const src_end,
    N       & target, node_size_type const tgt_begin
) noexcept
{
    BOOST_ASSUME( &source != &target ); // otherwise could require move_backwards or shift_*
    BOOST_ASSUME( src_begin <= src_end );
    BOOST_ASSUME( ( src_end - src_begin ) <= N::min_values + 1 );
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
    for ( node_size_type ch{ 0 }; ch < count; ++ch )
    {
        auto & ch_slot{ src_chldrn[ ch ] };
        auto & child  { node( ch_slot ) };
        target.children[ tgt_begin + ch ] = std::move( ch_slot );
        child.parent                      = target_slot;
        child.parent_child_idx            = tgt_begin + ch;
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

    using bptree_base::as;
    using bptree_base::can_borrow;
    using bptree_base::children;
    using bptree_base::ihalf_ceil;
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
    using base::leaf_level;
    using base::root;

public:
    using size_type       = base::size_type;
    using value_type      = base::value_type;
    using       pointer   = value_type       *;
    using const_pointer   = value_type const *;
    using       reference = value_type       &;
    using const_reference = value_type const &;
    using       iterator  = fwd_iterator;
    using const_iterator  = std::basic_const_iterator<iterator>;

    using base::size;
    using base::clear;

    static constexpr base::size_type max_size() noexcept
    {
        auto const max_number_of_nodes     { std::numeric_limits<typename node_slot::value_type>::max() };
        auto const max_number_of_leaf_nodes{ /*TODO*/ max_number_of_nodes };
        auto const values_per_node         { leaf_node::max_values };
        auto const max_sz                  { max_number_of_leaf_nodes * values_per_node };
        static_assert( max_sz <= std::numeric_limits<typename base::size_type>::max() );
        return max_sz;
    }

    [[ gnu::pure ]] iterator begin() noexcept { return { this->nodes_, this->first_leaf(), 0 }; } using stl_impl::begin;
    [[ gnu::pure ]] iterator end  () noexcept { return { this->nodes_, {}                , 0 }; } using stl_impl::end  ;

    [[ gnu::pure ]]                           ra_iterator  ra_begin()       noexcept { return { *this, this->first_leaf(), 0      }; }
    [[ gnu::pure ]]                           ra_iterator  ra_end  ()       noexcept { return { *this, {}                , size() }; }
    [[ gnu::pure ]] std::basic_const_iterator<ra_iterator> ra_begin() const noexcept { return const_cast<bp_tree &>( *this ).ra_begin(); }
    [[ gnu::pure ]] std::basic_const_iterator<ra_iterator> ra_end  () const noexcept { return const_cast<bp_tree &>( *this ).ra_end  (); }

    auto random_access()       noexcept { return std::ranges::subrange{ ra_begin(), ra_end() }; }
    auto random_access() const noexcept { return std::ranges::subrange{ ra_begin(), ra_end() }; }

    BOOST_NOINLINE
    void insert( key_const_arg v )
    {
        if ( base::empty() )
        {
            auto & root{ static_cast<leaf_node &>( base::create_root() ) };
            BOOST_ASSUME( root.num_vals == 1 );
            root.keys[ 0 ] = v;
            return;
        }

        auto const locations{ find_nodes_for( v ) };
        BOOST_ASSUME( !locations.inner );
        BOOST_ASSUME( !locations.inner_offset );
        BOOST_ASSUME( !locations.leaf_offset.exact_find );
        base::insert( locations.leaf, locations.leaf_offset.pos, v, { /*insertion starts from leaves which do not have children*/ } );

        ++this->hdr().size_;
    }

    BOOST_NOINLINE
    const_iterator find( key_const_arg key ) const noexcept {
        auto const location{ const_cast<bp_tree &>( *this ).find_nodes_for( key ) };
        if ( location.leaf_offset.exact_find ) [[ likely ]] {
            return iterator{ const_cast<bp_tree &>( *this ).nodes_, slot_of( location.leaf ), location.leaf_offset.pos };
        }

        return this->cend();
    }

    BOOST_NOINLINE
    void erase( key_const_arg key ) noexcept
    {
        auto & hdr{ base::hdr() };
        auto & __restrict depth_{ hdr.depth_ };
        auto & __restrict root_ { hdr.root_  };
        auto & __restrict size_ { hdr.size_  };

        auto const location{ find_nodes_for( key ) };
        // code below can go into base
        leaf_node & leaf{ location.leaf };
        if ( depth_ != 1 )
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


        BOOST_ASSUME( location.leaf_offset.exact_find );
        lshift_keys( leaf, leaf_key_offset );
        BOOST_ASSUME( leaf.num_vals );
        --leaf.num_vals;
        if ( depth_ == 1 ) [[ unlikely ]] // handle 'leaf root' deletion directly to simplify handle_underflow()
        {
            BOOST_ASSUME( root_ == slot_of( leaf ) );
            BOOST_ASSUME( leaf.is_root() );
            BOOST_ASSUME( size_ == leaf.num_vals + 1 );
            BOOST_ASSUME( !leaf.left  );
            BOOST_ASSUME( !leaf.right );
            if ( leaf.num_vals == 0 )
            {
                root_ = {};
                base::free( leaf );
                --depth_;
            }
        }
        else
        if ( base::underflowed( leaf ) )
        {
            BOOST_ASSUME( !leaf.is_root() );
            BOOST_ASSUME( depth_ > 1 );
            base::handle_underflow( leaf, base::leaf_level() );
        }

        --size_;
    }

    void swap( bp_tree & other ) noexcept { base::swap( other ); }

    Comparator const & comp() const noexcept { return *this; }

    // UB if the comparator is changed in such a way as to invalidate to order of elements already in the container
    [[ nodiscard ]] Comparator & mutable_comp() noexcept { return *this; }

private:
    struct find_pos
    {
        node_size_type pos        : ( sizeof( node_size_type ) * CHAR_BIT - 1 );
        node_size_type exact_find : 1;
    };
    [[ using gnu: pure, hot, noinline, sysv_abi ]]
    find_pos find( Key const keys[], node_size_type const num_vals, key_const_arg value ) const noexcept
    {
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
    auto find( auto const & node, key_const_arg value ) const noexcept
    {
        return find( node.keys, node.num_vals, value );
    }

    template <typename N>
    void insert( N & target_node, key_const_arg v, node_slot const right_child )
    {
        auto const pos{ find( target_node, v ).pos };
        base::insert( target_node, pos, v, right_child );
    }

    struct key_locations
    {
        leaf_node & leaf;
        find_pos    leaf_offset;
        // optional - if also present in an inner node as a separator key
        node_size_type inner_offset; // ordered for compact layout
        node_slot      inner;
    };

    [[ gnu::hot, gnu::sysv_abi ]]
    key_locations find_nodes_for( key_const_arg key ) noexcept
    {
        node_slot      separator_key_node;
        node_size_type separator_key_offset{};
        // if the root is a (lone) leaf_node is implicitly handled by the loop condition:
        // depth_ == 1 so the loop is skipped entirely and the lone root is never examined
        // through the incorrectly typed reference
        auto       p_node{ &bptree_base::as<parent_node>( root() ) };
        auto const depth { this->hdr().depth_ };
        BOOST_ASSUME( depth >= 1 );
        for ( auto level{ 0 }; level < depth - 1; ++level )
        {
            auto [pos, exact_find]{ find( *p_node, key ) };
            if ( exact_find )
            {
                // separator key - it also means we have to traverse to the right
                BOOST_ASSUME( !separator_key_node );
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

    [[ gnu::pure ]] bool le( key_const_arg left, key_const_arg right ) const noexcept { return comp()( left, right ); }
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
        if constexpr ( detail::is_simple_comparator<Comparator> && requires { left == right; } )
            return left <= right;
        return !comp()( right, left );
    }
}; // class bp_tree

PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
