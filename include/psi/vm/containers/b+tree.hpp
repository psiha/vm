#pragma once

#include <psi/vm/align.hpp>
#include <psi/vm/allocation.hpp>
#include <psi/vm/vector.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>
#include <boost/stl_interfaces/iterator_interface.hpp>
#include <boost/stl_interfaces/sequence_container_interface.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iterator>
#include <span>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

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
} // namespace detail

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

    storage_result map_file  ( auto      const file, flags::named_object_construction_policy const policy ) noexcept { return init_header( nodes_.map_file  ( file, policy              ) ); }
    storage_result map_memory( size_type const initial_capacity_in_nodes = 0                              ) noexcept { return init_header( nodes_.map_memory( initial_capacity_in_nodes ) ); }

protected:
    static constexpr auto node_size{ page_size };

    using depth_t = std::uint8_t;

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

        node_slot parent  {};
        size_type num_vals{};
      /* TODO
        size_type start;         // make keys and children arrays function as devectors: allow empty space at the beginning to avoid moves for small borrowings
        size_type in_parent_pos; // position of the node in its parent (to avoid finds in upscans).
      */

        [[ gnu::pure ]] bool is_root() const noexcept { return !parent; }

        // merely to prevent slicing (in return-node-by-ref cases)
        node_header( node_header const & ) = delete;
        constexpr node_header( node_header && ) noexcept = default;
        constexpr node_header(                ) noexcept = default;
        constexpr node_header & operator=( node_header && ) noexcept = default;
    }; // struct node_header
    using node_size_type = node_header::size_type;

    struct linked_node_header : node_header { node_slot next{}; /*singly linked free list*/ };

    struct alignas( node_size ) node_placeholder :        node_header {};
    struct alignas( node_size ) free_node        : linked_node_header {};

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

    [[ gnu::cold ]] linked_node_header & create_root();

    [[ gnu::pure ]] static bool underflowed( auto const & node ) noexcept { return node.num_vals < node.min_values; }
    [[ gnu::pure ]] static bool can_borrow ( auto const & node ) noexcept { return node.num_vals > node.min_values; }

    [[ gnu::pure ]] depth_t    leaf_level(                     ) const noexcept;
    [[ gnu::pure ]] bool    is_leaf_level( depth_t const level ) const noexcept;

    void free( node_header & ) noexcept;

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

    template <auto array, typename N>
    static void umove // uninitialized move
    (
        N const & source, node_size_type const srcBegin, node_size_type const srcEnd,
        N       & target, node_size_type const tgtBegin
    ) noexcept {
        BOOST_ASSUME( srcBegin <= srcEnd );
        std::uninitialized_move( &(source.*array)[ srcBegin ], &(source.*array)[ srcEnd ], &(target.*array)[ tgtBegin ] );
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

    [[ gnu::pure ]] linked_node_header & node() const noexcept;

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
    node_slot leaves_start_; // merely for the rewind for negative offsets in operator+=

    template <typename T, typename Comparator> friend class bp_tree;

    base_random_access_iterator( bptree_base & parent, node_slot const start_leaf, size_type const start_index ) noexcept
        : base_iterator{ parent.nodes_, start_leaf, 0 }, index_{ start_index }, leaves_start_{ parent.first_leaf() } {}

public:
    constexpr base_random_access_iterator() noexcept = default;

    difference_type operator-( base_random_access_iterator const & other ) const noexcept { return static_cast<difference_type>( this->index_ - other.index_ ); }

    base_random_access_iterator & operator+=( difference_type n ) noexcept;

    base_random_access_iterator & operator++(   ) noexcept { base_iterator::operator++(); ++index_; return *this; }
    base_random_access_iterator   operator++(int) noexcept { auto current{ *this }; operator++(); return current; }

    // should implicitly handle end iterator comparison also (this requires the start_index constructor argument for the construction of end iterators)
    friend constexpr bool operator==( base_random_access_iterator const & left, base_random_access_iterator const & right ) noexcept { return left.index_ == right.index_; }
}; // class base_random_access_iterator


////////////////////////////////////////////////////////////////////////////////
// \class bptree_base_wkey
////////////////////////////////////////////////////////////////////////////////

template <typename Key>
class bptree_base_wkey : public bptree_base
{
public:
    using key_type   = Key;
    using value_type = key_type; // TODO map support

    using key_const_arg = std::conditional_t<std::is_trivial_v<Key>, Key, Key const &>;

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

        static auto constexpr max_children{ order };
        static auto constexpr max_values  { max_children - 1 };
        
        node_slot children[ max_children ];
        Key       keys    [ max_values   ];
    }; // struct inner_node

    struct inner_node : parent_node
    {
        static auto constexpr min_children{ ( parent_node::max_children + 1 ) / 2 }; // ceil( m / 2 )
        static auto constexpr min_values  { min_children - 1 };

        static_assert( min_children >= 3 );
    }; // struct inner_node

    struct root_node : parent_node
    {
        static auto constexpr min_children{ 2 };
        static auto constexpr min_values  { min_children - 1 };
    }; // struct root_node

    struct alignas( node_size ) leaf_node : linked_node_header
    {
        // TODO support for maps (i.e. keys+values)
        using value_type = Key;

        static auto constexpr storage_space{ node_size - psi::vm::align_up( sizeof( linked_node_header ), alignof( Key ) ) };
        static auto constexpr max_values   { storage_space / sizeof( Key ) };
        static auto constexpr min_values   { ( max_values + 1 ) / 2 };

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

protected: // helpers for split_to_insert
    static value_type insert_into_new_node
    (
        parent_node & node, parent_node & new_node,
        key_const_arg value,
        node_size_type const insert_pos, node_size_type const new_insert_pos,
        node_slot const key_right_child
    ) noexcept {
        BOOST_ASSUME( bool( key_right_child ) );

        using N = parent_node;

        auto const max{ N::max_values }; // or 'order'
        auto const mid{ max / 2 };

        value_type key_to_propagate;
        if ( new_insert_pos == 0 ) {
            umove<&N::keys    >( node, mid    , node.num_vals    , new_node, 0 );
            umove<&N::children>( node, mid + 1, node.num_vals + 1, new_node, 1 );
            children( new_node )[ new_insert_pos ] = key_right_child;
            key_to_propagate = std::move( value );
        } else {
            key_to_propagate = std::move( node.keys[ mid ] );

            umove<&N::keys    >( node, mid + 1, insert_pos    , new_node, 0 );
            umove<&N::children>( node, mid + 1, insert_pos + 1, new_node, 0 );

            umove<&N::keys    >( node, insert_pos    , node.num_vals    , new_node, new_insert_pos     );
            umove<&N::children>( node, insert_pos + 1, node.num_vals + 1, new_node, new_insert_pos + 1 );

            keys    ( new_node )[ new_insert_pos - 1 ] = value;
            children( new_node )[ new_insert_pos     ] = key_right_child;
        }

        node    .num_vals = mid      ;
        new_node.num_vals = max - mid;

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

        auto const max{ N::max_values }; // or 'order'
        auto const mid{ max / 2 };

        umove<&N::keys>( node, mid       , insert_pos   , new_node, 0                  );
        umove<&N::keys>( node, insert_pos, node.num_vals, new_node, new_insert_pos + 1 );

        node    .num_vals = mid          ;
        new_node.num_vals = max - mid + 1;

        keys( new_node )[ new_insert_pos ] = value;
        auto const & key_to_propagate{ new_node.keys[ 0 ] };
        return key_to_propagate;
    }

    static value_type insert_into_existing_node( parent_node & node, parent_node & new_node, key_const_arg value, node_size_type const insert_pos, node_slot const key_right_child ) noexcept
    {
        BOOST_ASSUME( bool( key_right_child ) );

        using N = parent_node;

        auto const max{ N::max_values }; // or 'order'
        auto const mid{ max / 2 };

        value_type key_to_propagate{ std::move( node.keys[ mid - 1 ] ) };

        umove<&N::keys    >( node, mid, node.num_vals    , new_node, 0 );
        umove<&N::children>( node, mid, node.num_vals + 1, new_node, 0 );

        umove<&N::keys    >( node, insert_pos    , mid - 1, node, insert_pos + 1 );
        umove<&N::children>( node, insert_pos + 1, mid    , node, insert_pos + 2 );

        keys    ( node )[ insert_pos     ] = value;
        children( node )[ insert_pos + 1 ] = key_right_child;

        node    .num_vals = mid;
        new_node.num_vals = max - mid;

        return key_to_propagate;
    }

    static value_type const & insert_into_existing_node( leaf_node & node, leaf_node & new_node, key_const_arg value, node_size_type const insert_pos, node_slot const key_right_child ) noexcept
    {
        BOOST_ASSUME( !key_right_child );

        using N = leaf_node;

        auto const max{ N::max_values }; // or 'order'
        auto const mid{ max / 2 };

        umove<&N::keys>( node, mid       , node.num_vals, new_node, 0              );
        umove<&N::keys>( node, insert_pos, mid          , node    , insert_pos + 1 );

        node    .num_vals = mid + 1  ;
        new_node.num_vals = max - mid;

        keys( node )[ insert_pos ] = value;
        auto const & key_to_propagate{ new_node.keys[ 0 ] };
        return key_to_propagate;
    }

protected: // 'other'
    root_node       & root()       noexcept { return as<root_node>( bptree_base::root() ); }
    root_node const & root() const noexcept { return const_cast<bptree_base_wkey &>( *this ).root(); }

    using bptree_base::free;
    void free( leaf_node & node ) noexcept {
        auto & first_leaf{ hdr().leaves_ };
        if ( first_leaf == slot_of( node ) ) {
            first_leaf = node.next;
        }
        bptree_base::free( static_cast<node_header &>( node ) );
    }

    void merge_nodes
    (
        leaf_node & __restrict source, leaf_node & __restrict target,
        inner_node & __restrict parent, node_size_type const parent_key_idx, node_size_type const parent_child_idx
    ) noexcept
    {
        BOOST_ASSUME( source.num_vals <= source.max_values ); BOOST_ASSUME( source.num_vals >= source.min_values - 1 );
        BOOST_ASSUME( target.num_vals <= target.max_values ); BOOST_ASSUME( target.num_vals >= target.min_values - 1 );

        std::ranges::move( keys    ( source ), keys    ( target ).end() );
        std::ranges::move( children( source ), children( target ).end() );
        target.num_vals += source.num_vals;
        source.num_vals  = 0;
        target.next      = source.next;
        BOOST_ASSUME( target.num_vals == target.max_values - 1 );

        std::ranges::shift_left( keys    ( parent ).subspan( parent_key_idx   ), 1 );
        std::ranges::shift_left( children( parent ).subspan( parent_child_idx ), 1 );
        BOOST_ASSUME( parent.num_vals );
        parent.num_vals--;

        source.next = slot_of( target );
        verify( target );
        free  ( source );
        verify( parent );
    }

    void merge_nodes
    (
        inner_node & __restrict right, inner_node & __restrict left,
        inner_node & __restrict parent, node_size_type const parent_key_idx, node_size_type const parent_child_idx
    ) noexcept
    {
        BOOST_ASSUME( right.num_vals <= right.max_values ); BOOST_ASSUME( right.num_vals >= right.min_values - 1 );
        BOOST_ASSUME( left .num_vals <= left .max_values ); BOOST_ASSUME( left .num_vals >= left .min_values - 1 );

        for ( auto const ch_slot : children( right ) )
            this->node( ch_slot ).parent = slot_of( left );

        std::ranges::move( children( right ), children( left ).end() );
        left.num_vals += 1;
        auto & separator_key{ parent.keys[ parent_key_idx ] };
        keys( left ).back() = std::move( separator_key );
        std::ranges::move( keys( right ), keys( left ).end() );

        left .num_vals += right.num_vals;
        right.num_vals  = 0;
        BOOST_ASSUME( left.num_vals == left.max_values );
        verify( left  );
        free  ( right );

        std::ranges::shift_left( keys    ( parent ).subspan( parent_key_idx   ), 1 );
        std::ranges::shift_left( children( parent ).subspan( parent_child_idx ), 1 );
        BOOST_ASSUME( parent.num_vals );
        parent.num_vals--;
    }

public:
    // solely a debugging helper
    void print() const;
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
    using bptree_base::keys;
    using bptree_base::node;
    using bptree_base::num_chldrn;
    using bptree_base::slot_of;
    using bptree_base::underflowed;
    using bptree_base::verify;

    using base::free;
    using base::leaf_level;
    using base::root;

public:
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

    void swap( bp_tree & other ) noexcept { base::swap( other ); }

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

        insert( find_nodes_for( v ).leaf, v, { /*insertion starts from leaves which do not have children*/ } );

        ++this->hdr().size_;
    }

    BOOST_NOINLINE
    const_iterator find( key_const_arg key ) const noexcept {
        auto const & leaf{ const_cast<bp_tree &>( *this ).find_nodes_for( key ).leaf };
        auto const pos{ find( leaf, key ) };
        if ( pos != leaf.num_vals && leaf.keys[ pos ] == key ) [[ likely ]] {
            return iterator{ const_cast<bp_tree &>( *this ).nodes_, slot_of( leaf ), pos };
        }

        return this->cend();
    }

    BOOST_NOINLINE
    void erase( key_const_arg key ) noexcept
    {
        auto & __restrict depth_{ base::hdr().depth_ };
        auto & __restrict root_ { base::hdr().root_  };
        auto & __restrict size_ { base::hdr().size_  };

        auto const nodes{ find_nodes_for( key ) };

        leaf_node & leaf{ nodes.leaf };
        if ( depth_ != 1 )
            verify( leaf );
        auto const key_offset{ find( leaf, key ) };

        if ( nodes.inner ) [[ unlikely ]] // "most keys are in the leaf nodes"
        {
            BOOST_ASSUME( key_offset == 0 );
            BOOST_ASSUME( leaf.keys[ key_offset ] == key );

            auto * p_node{ &node<inner_node>( nodes.inner ) };
            auto & separator_key{ p_node->keys[ nodes.inner_offset ] };
            BOOST_ASSUME( separator_key == key );
            BOOST_ASSUME( key_offset + 1 < leaf.num_vals );
            separator_key = leaf.keys[ key_offset + 1 ];
        }


        BOOST_ASSUME( key_offset != leaf.num_vals && leaf.keys[ key_offset ] == key );
        std::ranges::shift_left( keys( leaf ).subspan( key_offset ), 1 );
        BOOST_ASSUME( leaf.num_vals );
        --leaf.num_vals;
        if ( depth_ == 1 ) [[ unlikely ]] // handle 'leaf root' deletion directly to simplify handle_underflow()
        {
            BOOST_ASSUME( root_ == slot_of( leaf ) );
            BOOST_ASSUME( leaf.is_root() );
            BOOST_ASSUME( size_ == leaf.num_vals + 1 );
            if ( leaf.num_vals == 0 ) {
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
            handle_underflow( leaf, base::leaf_level() );
        }

        --size_;
    }

    Comparator const & comp() const noexcept { return *this; }

    // UB if the comparator is changed in such a way as to invalidate to order of elements already in the container
    [[ nodiscard ]] Comparator & mutable_comp() noexcept { return *this; }

private:
    [[ gnu::pure, gnu::hot, clang::preserve_most, gnu::noinline ]]
    auto find( Key const keys[], node_size_type const num_vals, key_const_arg value ) const noexcept {
        BOOST_ASSUME( num_vals > 0 );
        auto const posIter { std::lower_bound( &keys[ 0 ], &keys[ num_vals ], value, comp() ) };
        auto const posIndex{ std::distance( &keys[ 0 ], posIter ) };
        return static_cast<node_size_type>( posIndex );
    }
    auto find( auto const & node, key_const_arg value ) const noexcept {
        return find( node.keys, node.num_vals, value );
    }

    template <typename N>
    void split_to_insert( N * p_node, node_size_type const insert_pos, key_const_arg value, node_slot const key_right_child ) {
        verify( *p_node );

        auto const max{ N::max_values }; // or 'order'
        auto const mid{ max / 2 };

        auto const node_slot{ slot_of( *p_node ) };
        auto * p_new_node{ &bptree_base::new_node<N>() };
        auto const new_slot{ slot_of( *p_new_node ) };
        p_node = &bptree_base::node<N>( node_slot ); // handle relocation
        BOOST_ASSUME( p_node->num_vals == max );

        auto constexpr parent_node_type{ requires{ p_new_node->children; } };

        p_new_node->parent = p_node->parent;
        if constexpr ( !parent_node_type )
        {
            // leaf linked list
            // p_node <- left/lesser | new_node -> right/greater
            p_new_node->next = p_node->next;
            p_node    ->next = new_slot;
        }

        auto const new_insert_pos         { insert_pos - mid  };
        auto const insertion_into_new_node{ insert_pos >= mid };
        Key key_to_propagate{ insertion_into_new_node
            ? base::insert_into_new_node     ( *p_node, *p_new_node, value, insert_pos, new_insert_pos, key_right_child )
            : base::insert_into_existing_node( *p_node, *p_new_node, value, insert_pos,                 key_right_child )
        };

        verify( *p_node     );
        verify( *p_new_node );

        if constexpr ( parent_node_type ) {
            for ( auto const ch_slot : children( *p_new_node ) ) {
                node( ch_slot ).parent = new_slot;
            }
        }

        // propagate the mid key to the parent
        if ( p_node->is_root() ) [[ unlikely ]] {
            auto & newRoot{ bptree_base::new_node<root_node>() };
            auto & hdr    { this->hdr() };
            p_node     = &node<N>( node_slot );
            p_new_node = &node<N>( new_slot );
            newRoot.keys    [ 0 ] = key_to_propagate;
            newRoot.children[ 0 ] = node_slot;
            newRoot.children[ 1 ] = new_slot;
            newRoot.num_vals      = 1;
            hdr.root_             = slot_of( newRoot );
            p_node    ->parent    = hdr.root_;
            p_new_node->parent    = hdr.root_;
            ++hdr.depth_;
        } else {
            return insert( node<inner_node>( p_node->parent ), key_to_propagate, new_slot );
        }
    }

    template <typename N>
    void insert( N & target_node, key_const_arg v, node_slot const rightChild ) {
        verify( target_node );
        auto const pos{ find( target_node, v ) };
        if ( base::full( target_node ) ) [[ unlikely ]] {
            return split_to_insert( &target_node, pos, v, rightChild );
        } else {
            ++target_node.num_vals;
            std::ranges::shift_right( base::keys( target_node ).subspan( pos ), 1 );
            target_node.keys[ pos ] = v;
            if constexpr ( requires { target_node.children; } ) {
                std::ranges::shift_right( children( target_node ).subspan( pos + /*>right< child*/1 ), 1 );
                target_node.children[ pos + 1 ] = rightChild;
            }
        }
    }


    template <typename N>
    BOOST_NOINLINE
    void handle_underflow( N & node, depth_t const level ) {
        auto & __restrict depth_{ this->hdr().depth_ };
        auto & __restrict root_ { this->hdr().root_  };

        BOOST_ASSUME( underflowed( node ) );

        auto constexpr parent_node_type{ requires{ node.children; } };
        auto constexpr leaf_node_type  { !parent_node_type };

        if ( node.is_root() ) [[ unlikely ]] {
            BOOST_ASSUME( level == 0      );
            BOOST_ASSUME( level <  depth_ ); // 'leaf root' should be handled directly by the special case in erase()
            BOOST_ASSUME( root_ == slot_of( node ) );
            if constexpr ( parent_node_type ) {
                BOOST_ASSUME( !!node.children[ 0 ] );
                root_ = node.children[ 0 ];
                BOOST_ASSUME( depth_ > 1 );
            }
            bptree_base::node<root_node>( root_ ).parent = {};
            free( node );
            --depth_;
            return;
        }

        BOOST_ASSUME( level > 0 );
        if constexpr ( leaf_node_type ) {
            BOOST_ASSUME( level == leaf_level() );
        }
        auto const node_slot{ slot_of( node ) };
        auto & parent{ this->node<inner_node>( node.parent ) };
        verify( parent );

        auto const parent_key_idx     { find( parent, node.keys[ 0 ] ) }; BOOST_ASSUME( parent_key_idx != parent.num_vals || !leaf_node_type );
        auto const parent_has_key_copy{ leaf_node_type && ( parent.keys[ parent_key_idx ] == node.keys[ 0 ] ) };
        auto const parent_child_idx   { parent_key_idx + parent_has_key_copy };
        BOOST_ASSUME( parent.children[ parent_child_idx ] == node_slot );

        auto const right_separator_key_idx{ parent_key_idx + parent_has_key_copy };
        auto const  left_separator_key_idx{ std::min<node_size_type>( right_separator_key_idx - 1, parent.num_vals ) }; // (ab)use unsigned wraparound
        auto const has_right_sibling      { right_separator_key_idx != parent.num_vals };
        auto const has_left_sibling       {  left_separator_key_idx != parent.num_vals };
        auto const p_right_separator_key  { has_right_sibling ? &parent.keys[ right_separator_key_idx ] : nullptr };
        auto const p_left_separator_key   { has_left_sibling  ? &parent.keys[  left_separator_key_idx ] : nullptr };
        BOOST_ASSUME( has_right_sibling == ( parent_child_idx < num_chldrn( parent ) - 1 ) );
        BOOST_ASSUME( has_left_sibling  == ( parent_child_idx > 0                        ) );
        auto const p_right_sibling{ has_right_sibling ? &this->node<N>( children( parent )[ parent_child_idx + 1 ] ) : nullptr };
        auto const p_left_sibling { has_left_sibling  ? &this->node<N>( children( parent )[ parent_child_idx - 1 ] ) : nullptr };

        BOOST_ASSERT( &node != p_left_sibling  );
        BOOST_ASSERT( &node != p_right_sibling );
        BOOST_ASSERT( static_cast<node_header *>( &node ) != static_cast<node_header *>( &parent ) );
        // Borrow from left sibling if possible
        if ( p_left_sibling && can_borrow( *p_left_sibling ) ) {
            verify( *p_left_sibling );

            node.num_vals++;
            BOOST_ASSUME( node.num_vals <= node.max_values );

            auto const node_vals{ keys( node ) };
            auto const left_vals{ keys( *p_left_sibling ) };
            std::ranges::shift_right( node_vals, 1 );
            
            if constexpr ( leaf_node_type ) {
                // Move the largest key from left sibling to the current node
                BOOST_ASSUME( parent_has_key_copy );
                node_vals.front() = std::move( left_vals.back() );
                // adjust the separator key in the parent
                BOOST_ASSERT( *p_left_separator_key == node_vals[ 1 ] );
                *p_left_separator_key = node_vals.front();
            } else {
                // Move/rotate the largest key from left sibling to the current node 'through' the parent
                BOOST_ASSERT( left_vals.back()      < *p_left_separator_key );
                BOOST_ASSERT( *p_left_separator_key < node_vals.front()     );
                node_vals.front()     = std::move( *p_left_separator_key );
                *p_left_separator_key = std::move( left_vals.back() );

                auto const chldrn{ children( node ) };
                std::ranges::shift_right( chldrn, 1 );
                chldrn.front()                      = std::move( children( *p_left_sibling ).back() );
                this->node( chldrn.front() ).parent = node_slot;
            }

            p_left_sibling->num_vals--;
            verify( *p_left_sibling );

            BOOST_ASSUME( node.           num_vals == N::min_values );
            BOOST_ASSUME( p_left_sibling->num_vals >= N::min_values );
        }
        // Borrow from right sibling if possible
        else if ( p_right_sibling && can_borrow( *p_right_sibling ) ) {
            verify( *p_right_sibling );

            node.num_vals++;
            BOOST_ASSUME( node.num_vals <= node.max_values );

            if constexpr ( leaf_node_type ) {
                // Move the smallest key from right sibling to current node the parent
                auto right_vals{ keys( *p_right_sibling ) };
                BOOST_ASSERT( parent.keys[ parent_child_idx ] == right_vals[ 0 ] );
                keys( node ).back() = std::move( right_vals.front() );
                std::ranges::shift_left( right_vals, 1 );
                // adjust the separator key in the parent
                *p_right_separator_key = right_vals.front();
            } else {
                // Move/rotate the smallest key from right sibling to current node 'through' the parent
                BOOST_ASSUME( parent.keys[ parent_child_idx ] < p_right_sibling->keys[ 0 ] );
                BOOST_ASSERT( *( keys( node ).end() - 2 ) < *p_right_separator_key );
                keys( node ).back()     = std::move( *p_right_separator_key );
                *p_right_separator_key  = std::move( keys    ( *p_right_sibling ).front() );
                children( node ).back() = std::move( children( *p_right_sibling ).front() );
                std::ranges::shift_left( keys    ( *p_right_sibling ), 1 );
                std::ranges::shift_left( children( *p_right_sibling ), 1 );
                this->node( children( node ).back() ).parent = node_slot;
            }
            
            p_right_sibling->num_vals--;
            verify( *p_right_sibling );

            BOOST_ASSUME( node.            num_vals == N::min_values );
            BOOST_ASSUME( p_right_sibling->num_vals >= N::min_values );
        }
        // Merge with left or right sibling
        else {
            if ( p_left_sibling ) {
                BOOST_ASSUME( parent_has_key_copy == leaf_node_type );
                // Merge node -> left sibling
                this->merge_nodes( node, *p_left_sibling, parent, left_separator_key_idx, parent_child_idx );
            } else {
                BOOST_ASSUME( parent_key_idx == 0 );
                BOOST_ASSUME( parent.keys[ parent_key_idx ] <= p_right_sibling->keys[ 0 ] );
                // Merge right sibling -> node
                this->merge_nodes( *p_right_sibling, node, parent, right_separator_key_idx, parent_child_idx + 1 );
            }

            // propagate underflow
            if (
                ( ( level == 1 ) && underflowed( as<root_node>( parent ) ) ) ||
                ( ( level != 1 ) && underflowed(                parent   ) )
            )
            {
                BOOST_ASSUME( level > 0      );
                BOOST_ASSUME( level < depth_ );
                handle_underflow( parent, level - 1 );
            }
        }
    }


    struct key_locations
    {
        leaf_node & leaf;
        // optional - if also present in an inner node as a separator key
        node_slot      inner;
        node_size_type inner_offset;
    };

    [[ gnu::hot, gnu::sysv_abi ]]
    key_locations find_nodes_for( key_const_arg key ) noexcept
    {
        node_slot      separator_key_node;
        node_size_type separator_key_offset;
        // if the root is a (lone) leaf_node is implicitly handled by the loop condition:
        // depth_ == 1 so the loop is skipped entirely and the lone root is never examined
        // through the incorrectly typed reference
        auto * p_node{ &bptree_base::as<parent_node>( root() ) };
        for ( auto level{ 0 }; level < this->hdr().depth_ - 1; ++level )
        {
            auto pos{ find( *p_node, key ) };
            if ( pos != p_node->num_vals && p_node->keys[ pos ] == key ) {
                // separator key - it also means we have to traverse to the right
                BOOST_ASSUME( !separator_key_node );
                separator_key_node   = slot_of( *p_node );
                separator_key_offset = pos;
                ++pos; // traverse to the right child
            }
            p_node = &node<parent_node>( p_node->children[ pos ] );
        }
        return { base::template as<leaf_node>( *p_node ), separator_key_node, separator_key_offset };
    }
}; // class bp_tree

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
