#include <psi/vm/containers/b+tree.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// https://en.wikipedia.org/wiki/B-tree
// https://opendsa-server.cs.vt.edu/ODSA/Books/CS3/html/BTree.html
// http://www.cburch.com/cs/340/reading/btree/index.html
// https://www.programiz.com/dsa/b-plus-tree
// https://www.geeksforgeeks.org/b-trees-implementation-in-c
// https://github.com/jeffplaisance/BppTree
// https://flatcap.github.io/linux-ntfs/ntfs/concepts/tree/index.html
// https://benjamincongdon.me/blog/2021/08/17/B-Trees-More-Than-I-Thought-Id-Want-to-Know
// https://stackoverflow.com/questions/59362113/b-tree-minimum-internal-children-count-explanation
// https://web.archive.org/web/20190126073810/http://supertech.csail.mit.edu/cacheObliviousBTree.html
// https://www.researchgate.net/publication/220225482_Cache-Oblivious_Databases_Limitations_and_Opportunities
// https://www.postgresql.org/docs/current/btree.html
// https://abseil.io/about/design/btree
// Data Structure Visualizations https://www.cs.usfca.edu/~galles/visualization/Algorithms.html
// Griffin: Fast Transactional Database Index with Hash and B+-Tree https://ieeexplore.ieee.org/abstract/document/10678674
// Restructuring the concurrent B+-tree with non-blocked search operations https://www.sciencedirect.com/science/article/abs/pii/S002002550200261X

// https://en.wikipedia.org/wiki/Judy_array

bptree_base::bptree_base( header_info const hdr_info ) noexcept
    :
    nodes_{ hdr_info.add_header<header>() }
{}

void bptree_base::clear() noexcept
{
    nodes_.clear();
    hdr() = {};
}

std::span<std::byte>
bptree_base::user_header_data() noexcept { return header_data().second; }

bptree_base::header &
bptree_base::hdr() noexcept { return *header_data().first; }

bptree_base::storage_result
bptree_base::init_header( storage_result success ) noexcept
{
    if ( std::move( success ) && nodes_.empty() )
        hdr() = {};
    return success;
}

void bptree_base::reserve( node_slot::value_type additional_nodes )
{
    auto const preallocated_count{ hdr().free_node_count_ };
    additional_nodes -= std::min( preallocated_count, additional_nodes );
    auto const current_size{ nodes_.size() };
    nodes_.grow_by( additional_nodes, value_init );
#ifndef NDEBUG
    hdr_ = &hdr();
#endif
    for ( auto & n : std::views::reverse( std::span{ nodes_ }.subspan( current_size ) ) )
        free( n );
}

void bptree_base::rshift_sibling_parent_pos( node_header & node ) noexcept
{
    auto p_node{ &node };
    while ( p_node->right )
    {
        auto & right{ this->node( p_node->right ) };
        BOOST_ASSUME( right.parent_child_idx == p_node->parent_child_idx );
        ++right.parent_child_idx;
        p_node = &right;
    }
}

void bptree_base::update_right_sibling_link( node_header const & left_node, node_slot const left_node_slot ) noexcept
{
    BOOST_ASSUME( slot_of( left_node ) == left_node_slot );
    if ( left_node.right ) [[ likely ]]
    {
        auto & right_back_left{ this->node( left_node.right ).left };
        BOOST_ASSUME( right_back_left != left_node_slot );
        right_back_left = left_node_slot;
    }
}

void bptree_base::unlink_node( node_header & node, node_header & cached_left_sibling ) noexcept
{
    auto & left { cached_left_sibling };
    auto & right{ node };
    BOOST_ASSUME( left .right == slot_of( right ) );
    BOOST_ASSUME( right.left  == slot_of( left  ) );
    left.right = right.right;
    update_right_sibling_link( left, right.left );
}

[[ gnu::noinline, gnu::sysv_abi ]]
std::pair<bptree_base::node_slot, bptree_base::node_slot>
bptree_base::new_spillover_node_for( node_header & existing_node )
{
    auto   const existing_node_slot{ slot_of( existing_node ) };
    auto &       right_node        { new_node() };
    auto &       left_node         { node( existing_node_slot ) };  // handle relocation (by new_node())
    auto   const right_node_slot   { slot_of( right_node ) };

    // insert into the level dlinked list (TODO extract into small specialized dlinked list functions)
    // node <- left/lesser | new_node -> right/greater
    right_node.left  = existing_node_slot;
    right_node.right = left_node.right;
     left_node.right = right_node_slot;
    update_right_sibling_link( right_node, right_node_slot );
    right_node.parent           = left_node.parent;
    right_node.parent_child_idx = left_node.parent_child_idx + 1;
    //right-sibling parent_child_idx rshifting is performed by insert_into_*
    //rshift_sibling_parent_pos( right_node );

    return std::make_pair( existing_node_slot, right_node_slot );
}
[[ gnu::noinline ]]
bptree_base::node_placeholder &
bptree_base::new_root( node_slot const left_child, node_slot const right_child )
{
    auto & new_root{ new_node() };
    auto & hdr     { this->hdr() };
    auto & left { node(  left_child ) }; BOOST_ASSUME( left.is_root() );
    auto & right{ node( right_child ) };
    new_root.left = new_root.right = {};
    new_root.num_vals = 1; // the minimum of two children with one separator key
    hdr.root_         = slot_of( new_root );
    left .parent      = hdr.root_;
    right.parent      = hdr.root_;
    BOOST_ASSUME( left .parent_child_idx == 0 );
    BOOST_ASSUME( right.parent_child_idx == 1 );
    ++hdr.depth_;
    return new_root;
}

bptree_base::base_iterator::base_iterator( node_pool & nodes, iter_pos const pos ) noexcept
    :
    nodes_{},
    pos_{ pos }
{
    update_pool_ptr( nodes );
}

void bptree_base::base_iterator::update_pool_ptr( node_pool & nodes ) const noexcept
{
#ifndef NDEBUG // for bounds checking
    nodes_ = nodes;
#else
    nodes_ = nodes.data();
#endif
}

[[ gnu::pure ]]
bptree_base::node_header &
bptree_base::base_iterator::node() const noexcept { return nodes_[ *pos_.node ]; }

bptree_base::base_iterator &
bptree_base::base_iterator::operator++() noexcept
{
    auto & node{ this->node() };
    BOOST_ASSERT_MSG( pos_.value_offset < node.num_vals, "Iterator at end: not incrementable" );
    BOOST_ASSUME( node.num_vals >= 1 );
    BOOST_ASSUME( pos_.value_offset < node.num_vals );
    if ( ++pos_.value_offset == node.num_vals ) [[ unlikely ]]
    {
        // have to perform this additional check to allow an iterator to arrive
        // to the end position
        if ( node.right ) [[ likely ]]
        {
            pos_.node         = node.right;
            pos_.value_offset = 0;
        }
    }
    return *this;
}
bptree_base::base_iterator &
bptree_base::base_iterator::operator--() noexcept
{
    auto & node{ this->node() };
    BOOST_ASSERT_MSG( ( pos_.value_offset > 0 ) || node.left, "Iterator at end: not incrementable" );
    BOOST_ASSUME( pos_.value_offset <= node.num_vals );
    BOOST_ASSUME( node.num_vals >= 1 );
    if ( pos_.value_offset-- == 0 ) [[ unlikely ]]
    {
        pos_.node         = node.left;
        pos_.value_offset = this->node().num_vals - 1;
    }
    return *this;
}

bool bptree_base::base_iterator::operator==( base_iterator const & other ) const noexcept
{
    BOOST_ASSERT_MSG( &this->nodes_[ 0 ] == &other.nodes_[ 0 ], "Comparing iterators from different containers" );
    return ( this->pos_.node == other.pos_.node ) && ( this->pos_.value_offset == other.pos_.value_offset );
}


bptree_base::base_random_access_iterator &
bptree_base::base_random_access_iterator::operator+=( difference_type const n ) noexcept
{
    BOOST_ASSERT_MSG( pos_.node, "Iterator at end: not incrementable" );

    if ( n >= 0 )
    {
        auto un{ static_cast<size_type>( n ) };
        for ( ;; )
        {
            auto & node{ this->node() };
            auto const available_offset{ static_cast<size_type>( node.num_vals - 1 - pos_.value_offset ) };
            if ( available_offset >= un ) {
                pos_.value_offset += static_cast<node_size_type>( un );
                BOOST_ASSUME( pos_.value_offset < node.num_vals );
                break;
            } else {
                un -= ( available_offset + 1 );
                // Here we don't have to perform the same check as in the
                // fwd_iterator increment as (end) iterator comparison is done
                // solely through the index_ member.
                pos_.node         = node.right;
                pos_.value_offset = 0;
                if ( un == 0 ) [[ unlikely ]]
                    break;
                BOOST_ASSERT_MSG( pos_.node, "Incrementing out of bounds" );
            }
        }
        index_ += static_cast<size_type>( n );
    }
    else
    {
        auto un{ static_cast<size_type>( -n ) };
        BOOST_ASSERT_MSG( index_ >= un, "Moving iterator out of bounds" );
        for ( ;; )
        {
            auto const available_offset{ pos_.value_offset };
            if ( available_offset >= un ) {
                pos_.value_offset -= static_cast<node_size_type>( un );
                BOOST_ASSUME( pos_.value_offset < node().num_vals );
                break;
            } else {
                un -= ( available_offset + 1 );
                pos_.node         = node().left;
                pos_.value_offset = node().num_vals - 1;
                BOOST_ASSERT_MSG( pos_.node, "Incrementing out of bounds" );
                BOOST_ASSUME( pos_.value_offset < node().num_vals );
            }
        }
        index_ -= static_cast<size_type>( -n );
    }

    BOOST_ASSUME( !pos_.node || ( pos_.value_offset < node().num_vals ) );

    return *this;
}


void bptree_base::swap( bptree_base & other ) noexcept
{
    using std::swap;
    swap( this->nodes_, other.nodes_ );
#ifndef NDEBUG
    swap( this->hdr_, other.hdr_ );
#endif
}

[[ gnu::pure ]] bptree_base::iter_pos bptree_base::begin_pos() const noexcept { return { this->first_leaf(), 0 }; }
[[ gnu::pure ]] bptree_base::iter_pos bptree_base::  end_pos() const noexcept {
    auto const last_leaf{ hdr().last_leaf_ };
    return { last_leaf, node( last_leaf ).num_vals };
}

[[ gnu::pure ]] bptree_base::base_iterator bptree_base::begin() noexcept { return { this->nodes_, begin_pos() }; }
[[ gnu::pure ]] bptree_base::base_iterator bptree_base::end  () noexcept { return { this->nodes_,   end_pos() }; }

[[ gnu::pure ]] bptree_base::base_random_access_iterator bptree_base::ra_begin() noexcept { return { *this, begin_pos(), 0      }; }
[[ gnu::pure ]] bptree_base::base_random_access_iterator bptree_base::ra_end  () noexcept { return { *this,   end_pos(), size() }; }

[[ gnu::cold ]]
bptree_base::node_header &
bptree_base::create_root()
{
    BOOST_ASSUME( !hdr().root_  );
    BOOST_ASSUME( !hdr().depth_ );
    BOOST_ASSUME( !hdr().size_  );
    // the initial/'lone' root node is a leaf node
    auto & root{ new_node() };
    auto & hdr { this->hdr() };
    BOOST_ASSUME( root.num_vals == 0 );
    root.num_vals   = 1;
    root.left       = {};
    root.right      = {};
    hdr.root_       = slot_of( root );
    hdr.first_leaf_ = hdr.root_;
    hdr.last_leaf_  = hdr.root_;
    hdr.depth_      = 1;
    hdr.size_       = 1;
    return root;
}

bptree_base::depth_t bptree_base::   leaf_level() const noexcept { BOOST_ASSUME( hdr().depth_ ); return hdr().depth_ - 1; }
             bool    bptree_base::is_leaf_level( depth_t const level ) const noexcept { return level == leaf_level(); }

void bptree_base::free( node_header & node ) noexcept
{
    BOOST_ASSUME( node.num_vals == 0 );
    auto & hdr{ this->hdr() };
    auto & free_list{ hdr.free_list_ };
    auto & free_node{ static_cast<struct free_node &>( node ) };
    auto const free_node_slot{ slot_of( free_node ) };
#ifndef NDEBUG
    if ( free_node.left )
        BOOST_ASSERT( this->node( free_node.left ).right != free_node_slot );
    if ( free_node.right )
        BOOST_ASSERT( this->node( free_node.right ).left != free_node_slot );
#endif
    free_node.left = {};
    if ( free_list ) { BOOST_ASSUME(  hdr.free_node_count_ ); free_node.right = free_list; this->node<struct free_node>( free_list ).left = free_node_slot; }
    else             { BOOST_ASSUME( !hdr.free_node_count_ ); free_node.right = {}       ; }
    free_list = free_node_slot;
    ++hdr.free_node_count_;
}

bptree_base::node_slot
bptree_base::slot_of( node_header const & node ) const noexcept
{
    return { static_cast<node_slot::value_type>( static_cast<node_placeholder const *>( &node ) - nodes_.data() ) };
}


[[ gnu::noinline ]]
bptree_base::node_placeholder &
bptree_base::new_node()
{
    auto & hdr      { this->hdr() };
    auto & free_list{ hdr.free_list_ };
    if ( free_list )
    {
        BOOST_ASSUME( hdr.free_node_count_ );
        auto & cached_node{ node<free_node>( free_list ) };
        BOOST_ASSUME( !cached_node.num_vals );
        BOOST_ASSUME( !cached_node.left     );
        free_list = cached_node.right;
        cached_node.right = {};
        if ( free_list )
            node( free_list ).left  = {};
        --hdr.free_node_count_;
        return as<node_placeholder>( cached_node );
    }
    auto & new_node{ nodes_.emplace_back() };
    BOOST_ASSUME( new_node.num_vals == 0 );
    new_node.left = new_node.right = {};
#ifndef NDEBUG
    hdr_ = &this->hdr();
#endif
    return new_node;
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
