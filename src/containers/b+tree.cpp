#include <psi/vm/containers/b+tree.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// https://en.wikipedia.org/wiki/B-tree
// https://en.wikipedia.org/wiki/B%2B_tree
// https://opendsa-server.cs.vt.edu/ODSA/Books/CS3/html/BTree.html
// http://www.cburch.com/cs/340/reading/btree/index.html
// https://www.programiz.com/dsa/b-plus-tree
// https://courses.cs.washington.edu/courses/cse332/23su/lectures/9_B_Trees.pdf (version, as this impl, which has different key counts in inner vs leaf nodes)
// https://www.geeksforgeeks.org/b-trees-implementation-in-c
// https://flatcap.github.io/linux-ntfs/ntfs/concepts/tree/index.html
// https://benjamincongdon.me/blog/2021/08/17/B-Trees-More-Than-I-Thought-Id-Want-to-Know
// https://stackoverflow.com/questions/59362113/b-tree-minimum-internal-children-count-explanation
// https://web.archive.org/web/20190126073810/http://supertech.csail.mit.edu/cacheObliviousBTree.html
// https://www.researchgate.net/publication/220225482_Cache-Oblivious_Databases_Limitations_and_Opportunities
// https://www.postgresql.org/docs/current/btree.html
// https://www.scylladb.com/2021/11/23/the-taming-of-the-b-trees
// https://www.scattered-thoughts.net/writing/smolderingly-fast-btrees
// B+tree vs LSM-tree https://www.usenix.org/conference/fast22/presentation/qiao
// Data Structure Visualizations https://www.cs.usfca.edu/~galles/visualization/Algorithms.html
// Griffin: Fast Transactional Database Index with Hash and B+-Tree https://ieeexplore.ieee.org/abstract/document/10678674
// Restructuring the concurrent B+-tree with non-blocked search operations https://www.sciencedirect.com/science/article/abs/pii/S002002550200261X
// Cache-Friendly Search Trees https://arxiv.org/pdf/1907.01631
// ZBTree: A Fast and Scalable B+-Tree for Persistent Memory https://ieeexplore.ieee.org/document/10638243

// https://abseil.io/about/design/btree
// https://github.com/tlx/tlx/tree/master/tlx/container
// https://github.com/jeffplaisance/BppTree
// https://github.com/postgres/postgres/blob/master/src/backend/access/nbtree/README
// https://github.com/scylladb/scylladb/blob/master/utils/intrusive_btree.hh

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

[[ gnu::pure, gnu::hot, gnu::always_inline ]]
bptree_base::header &
bptree_base::hdr() noexcept { return *header_data().first; }

bptree_base::storage_result
bptree_base::map_memory( std::uint32_t const initial_capacity_as_number_of_nodes ) noexcept
{
    auto success{ nodes_.map_memory( initial_capacity_as_number_of_nodes, value_init )() };
    if ( success )
    {
        hdr() = {};
        if ( initial_capacity_as_number_of_nodes ) {
            assign_nodes_to_free_pool( 0 );
        }
#   ifndef NDEBUG
        p_hdr_   = &hdr();
        p_nodes_ = nodes_.data();
#   endif
    }
    return success;
}
[[ gnu::cold ]]
void bptree_base::reserve_additional( node_slot::value_type additional_nodes )
{
    auto const preallocated_count{ hdr().free_node_count_ };
    additional_nodes -= std::min( preallocated_count, additional_nodes );
    auto const current_size{ nodes_.size() };
    nodes_.grow_by( additional_nodes, value_init );
#ifndef NDEBUG
    p_hdr_   = &hdr();
    p_nodes_ = nodes_.data();
#endif
    assign_nodes_to_free_pool( current_size );
}
[[ gnu::cold ]]
void bptree_base::reserve( node_slot::value_type new_capacity_in_number_of_nodes )
{
    if ( new_capacity_in_number_of_nodes <= nodes_.capacity() )
        return;
    auto const current_size{ nodes_.size() };
    nodes_.grow_to( new_capacity_in_number_of_nodes, value_init );
#ifndef NDEBUG
    p_hdr_   = &hdr();
    p_nodes_ = nodes_.data();
#endif
    assign_nodes_to_free_pool( current_size );
}
[[ gnu::cold ]]
void bptree_base::assign_nodes_to_free_pool( node_slot::value_type const starting_node ) noexcept
{
    for ( auto & n : std::views::reverse( std::span{ nodes_ }.subspan( starting_node ) ) )
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
        auto & right_back_left{ right( left_node ).left };
        BOOST_ASSUME( right_back_left != left_node_slot );
        right_back_left = left_node_slot;
    }
}

void bptree_base::unlink_and_free_node( node_header & node, node_header & cached_left_sibling ) noexcept
{
    auto & left { cached_left_sibling };
    auto & right{ node };
    BOOST_ASSUME( left .right == slot_of( right ) );
    BOOST_ASSUME( right.left  == slot_of( left  ) );
    left.right = right.right;
    update_right_sibling_link( left, right.left );
    free( node );
}

void bptree_base::update_leaf_list_ends( node_header & removed_leaf ) noexcept
{
    auto & first_leaf{ hdr().first_leaf_ };
    auto &  last_leaf{ hdr().last_leaf_  };
    auto const slot{ slot_of( removed_leaf ) };
    if ( slot == first_leaf ) [[ unlikely ]]
    {
        BOOST_ASSUME( !removed_leaf.left );
        first_leaf = removed_leaf.right;
    }
    if ( slot == last_leaf ) [[ unlikely ]]
    {
        BOOST_ASSUME( !removed_leaf.right );
        last_leaf = removed_leaf.left;
    }
}

void bptree_base::unlink_and_free_leaf( node_header & leaf, node_header & cached_left_sibling ) noexcept
{
    update_leaf_list_ends( leaf );
    unlink_and_free_node( leaf, cached_left_sibling );
}


void bptree_base::unlink_left( node_header & nd ) noexcept
{
    if ( !nd.left )
        return;
    auto & left_link{ left( nd ).right };
    BOOST_ASSUME( left_link == slot_of( nd ) );
    left_link = nd.left = {};
}

void bptree_base::unlink_right( node_header & nd ) noexcept
{
    if ( !nd.right )
        return;
    auto & right_link{ right( nd ).left };
    BOOST_ASSUME( right_link == slot_of( nd ) );
    right_link = nd.right = {};
}

void bptree_base::link( node_header & left, node_header & right ) const noexcept
{
    BOOST_ASSUME( !left .right );
    BOOST_ASSUME( !right.left  );
    left .right = slot_of( right );
    right.left  = slot_of( left  );
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

    BOOST_ASSERT( !pos_.node || ( pos_.value_offset <= node().num_vals ) );
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

[[ clang::no_sanitize( "implicit-conversion" ) ]]
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

bptree_base::base_iterator &
bptree_base::base_iterator::operator+=( difference_type const n ) noexcept
{
#if __has_builtin( __builtin_constant_p )
    if ( __builtin_constant_p( n ) )
    {
             if ( n == +1 ) return ++(*this);
        else if ( n == -1 ) return --(*this);
        else if ( n ==  0 ) return   (*this);
    }
#endif

    if ( n >= 0 )
    {
        auto const un{ static_cast<size_type>( n ) };
        this->pos_ = at_positive_offset<true>( un );
    }
    else
    {
        auto const un{ static_cast<size_type>( -n ) };
        this->pos_ = at_negative_offset( un );
    }

    BOOST_ASSUME( !pos_.node || ( pos_.value_offset < node().num_vals ) );

    return *this;
}

template <bool precise_end_handling> [[ using gnu: sysv_abi, hot, const ]]
bptree_base::base_iterator
bptree_base::base_iterator::incremented( this base_iterator iter ) noexcept
{
    auto & node{ iter.node() };
    BOOST_ASSERT_MSG( iter.pos_.value_offset < node.num_vals, "Iterator at end: not incrementable" );
    BOOST_ASSUME( node.num_vals >= 1 );
    BOOST_ASSUME( iter.pos_.value_offset < node.num_vals );
    if ( ++iter.pos_.value_offset == node.num_vals ) [[ unlikely ]]
    {
        // additional check to allow the iterator to arrive to the end position
        // (if required by the precise_end_handling flag)
        if ( !precise_end_handling || node.right ) [[ likely ]]
        {
            iter.pos_.node         = node.right;
            iter.pos_.value_offset = 0;
        }
    }
    return iter;
}
template bptree_base::base_iterator bptree_base::base_iterator::incremented<true >( this base_iterator ) noexcept;
template bptree_base::base_iterator bptree_base::base_iterator::incremented<false>( this base_iterator ) noexcept;

template <bool precise_end_handling> [[ using gnu: noinline, hot, leaf, const ]][[ clang::preserve_most ]]
bptree_base::iter_pos
bptree_base::base_iterator::at_positive_offset( nodes_t const nodes, iter_pos const pos, size_type n ) noexcept
{
    BOOST_ASSERT_MSG( pos.node || !n, "Iterator at end: not incrementable" );
    base_iterator iter{ nodes, pos };
    for ( ;; )
    {
        auto & node{ iter.node() };
        auto const available_offset{ static_cast<size_type>( node.num_vals - 1 - iter.pos_.value_offset ) };
        if ( available_offset >= n ) {
            iter.pos_.value_offset += static_cast<node_size_type>( n );
            BOOST_ASSUME( iter.pos_.value_offset < node.num_vals );
            break;
        } else {
            n -= ( available_offset + 1 );
            if ( !precise_end_handling || node.right ) [[ likely ]]
            {
                iter.pos_.node         = node.right;
                iter.pos_.value_offset = 0;
            }
            if ( n == 0 ) [[ unlikely ]]
                break;
            BOOST_ASSERT_MSG( iter.pos_.node, "Incrementing out of bounds" );
        }
    }
    return iter.pos_;
}
template bptree_base::iter_pos bptree_base::base_iterator::at_positive_offset<true >( nodes_t, iter_pos, size_type ) noexcept;
template bptree_base::iter_pos bptree_base::base_iterator::at_positive_offset<false>( nodes_t, iter_pos, size_type ) noexcept;

[[ using gnu: noinline, hot, leaf, const ]][[ clang::preserve_most ]]
bptree_base::iter_pos
bptree_base::base_iterator::at_negative_offset( nodes_t const nodes, iter_pos const pos, size_type n ) noexcept
{
    base_iterator iter{ nodes, pos };
    for ( ;; )
    {
        auto const available_offset{ iter.pos_.value_offset };
        if ( available_offset >= n ) {
            iter.pos_.value_offset -= static_cast<node_size_type>( n );
            BOOST_ASSUME( iter.pos_.value_offset < iter.node().num_vals );
            break;
        } else {
            n -= ( available_offset + 1 );
            iter.pos_.node         = iter.node().left;
            iter.pos_.value_offset = iter.node().num_vals - 1;
            BOOST_ASSERT_MSG( iter.pos_.node, "Incrementing out of bounds" );
            BOOST_ASSUME( iter.pos_.value_offset < iter.node().num_vals );
        }
    }
    return iter.pos_;
}

bool bptree_base::base_iterator::operator==( base_iterator const & other ) const noexcept
{
    BOOST_ASSERT_MSG( &this->nodes_[ 0 ] == &other.nodes_[ 0 ], "Comparing iterators from different containers" );
#ifdef NDEBUG
    BOOST_ASSUME( this->nodes_ == other.nodes_ );
#endif
    return this->pos_ == other.pos_;
}

[[ using gnu: sysv_abi, hot, pure ]]
bptree_base::base_random_access_iterator
bptree_base::base_random_access_iterator::at_offset( difference_type const n ) const noexcept
{
    iter_pos new_pos;
    if ( n >= 0 )
    {
        auto const un{ static_cast<size_type>( n ) };
        // Here we don't have to perform the same check as in the generic/
        // fwd_iterator increment since (end) iterator comparison is done solely
        // through the index_ member...
        // ...but we do need it if we want the 'arrived at end iterators' to be
        // decrementable (and some sort algorithms rely on this).
        new_pos = at_positive_offset<true>( un );
    }
    else
    {
        auto const un{ static_cast<size_type>( -n ) };
        BOOST_ASSERT_MSG( index_ >= un, "Moving iterator out of bounds" );
        new_pos = at_negative_offset( un );
    }

    return { base_iterator{ nodes_, new_pos }, static_cast<size_type>( static_cast<difference_type>( index_ ) + n ) };
}

void bptree_base::swap( bptree_base & other ) noexcept
{
    using std::swap;
    swap( this->nodes_, other.nodes_ );
#ifndef NDEBUG
    swap( this->p_hdr_  , other.p_hdr_   );
    swap( this->p_nodes_, other.p_nodes_ );
#endif
}


bptree_base::base_iterator bptree_base::make_iter( iter_pos const pos ) noexcept { return { nodes_, pos }; }
bptree_base::base_iterator bptree_base::make_iter( node_slot const node, node_size_type const offset ) noexcept { return make_iter(iter_pos{ node, offset }); }
bptree_base::base_iterator bptree_base::make_iter( node_header const & node, node_size_type const offset ) noexcept { return make_iter( slot_of( node ), offset ); }
bptree_base::base_iterator bptree_base::make_iter( insert_pos_t const next_pos ) noexcept
{
    auto iter{ make_iter( next_pos.node, next_pos.next_insert_offset ) };
    --iter;
    return iter;
}

[[ gnu::pure ]] bptree_base::iter_pos bptree_base::begin_pos() const noexcept { return { this->first_leaf(), 0 }; }
[[ gnu::pure ]] bptree_base::iter_pos bptree_base::  end_pos() const noexcept {
    auto const last_leaf{ hdr().last_leaf_ };
    return { last_leaf, last_leaf ? node( last_leaf ).num_vals : node_size_type{} };
}

[[ gnu::pure ]] bptree_base::base_iterator bptree_base::begin() noexcept { return make_iter( begin_pos() ); }
[[ gnu::pure ]] bptree_base::base_iterator bptree_base::end  () noexcept { return make_iter(   end_pos() ); }

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

bool bptree_base::is_my_node( node_header const & node ) const noexcept
{
    return ( &node >= &nodes_.front() ) && ( &node <= &nodes_.back() );
}

bptree_base::node_slot
bptree_base::slot_of( node_header const & node ) const noexcept
{
    BOOST_ASSUME( is_my_node( node ) );
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
        unlink_right( cached_node );
        --hdr.free_node_count_;
        return as<node_placeholder>( cached_node );
    }
    auto & new_node{ nodes_.emplace_back() };
    BOOST_ASSUME( !new_node.num_vals );
    BOOST_ASSUME( !new_node.left     );
    BOOST_ASSUME( !new_node.right    );
#ifndef NDEBUG
    p_hdr_   = &this->hdr();
    p_nodes_ = nodes_.data();
#endif
    return new_node;
}

void bptree_base::free( node_header & node ) noexcept
{
    auto & hdr{ this->hdr() };
    auto & free_list{ hdr.free_list_ };
    auto & free_node{ static_cast<struct free_node &>( node ) };
    auto const free_node_slot{ slot_of( free_node ) };
    BOOST_ASSUME( free_node_slot != hdr.last_leaf_ ); // should have been handled in the dedicated overload
#ifndef NDEBUG
    if ( free_node.left )
        BOOST_ASSERT( left( free_node ).right != free_node_slot );
    if ( free_node.right )
        BOOST_ASSERT( right( free_node ).left != free_node_slot );
#endif
    // TODO (re)consider whether the calling code is expected to reset num_vals
    // (and thus mark the node as 'free'/empty) and/or unlink from its siblings
    // and the parent (which complicates code but possibly catches errors
    // earlier) or have free perform all of the cleanup.
    //BOOST_ASSUME( node.num_vals == 0 );
    // We have to touch the header (i.e. its residing cache line) anyway here
    // (to update the right link) so reset/setup the whole header right now for
    // the new allocation step.
    static_cast<node_header &>( free_node ) = {};
    // update the right link
    if ( free_list ) { BOOST_ASSUME(  hdr.free_node_count_ ); link( free_node, this->node( free_list ) ); }
    else             { BOOST_ASSUME( !hdr.free_node_count_ ); }
    free_list = free_node_slot;
    ++hdr.free_node_count_;
}

void bptree_base::free_leaf( node_header & leaf ) noexcept
{
    update_leaf_list_ends( leaf );
    free( leaf );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
