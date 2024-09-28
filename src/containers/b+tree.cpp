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
// Data Structure Visualizations https://www.cs.usfca.edu/~galles/visualization/Algorithms.html
// Griffin: Fast Transactional Database Index with Hash and B+-Tree https://ieeexplore.ieee.org/abstract/document/10678674
// Restructuring the concurrent B+-tree with non-blocked search operations https://www.sciencedirect.com/science/article/abs/pii/S002002550200261X

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

void bptree_base::reserve( node_slot::value_type const additional_nodes )
{
    auto const current_size{ nodes_.size() };
    nodes_.grow_by( additional_nodes, value_init );
    for ( auto & n : std::views::reverse( std::span{ nodes_ }.subspan( current_size ) ) )
        free( n );
}

bptree_base::base_iterator::base_iterator( node_pool & nodes, node_slot const node_offset, node_size_type const value_offset ) noexcept
    :
#ifndef NDEBUG // for bounds checking
    nodes_{ nodes },
#else
    nodes_{ nodes.data() },
#endif
    node_slot_{ node_offset }, value_offset_{ value_offset }
{}

bptree_base::linked_node_header &
bptree_base::base_iterator::node() const noexcept { return static_cast<linked_node_header &>( static_cast<node_header &>( nodes_[ *node_slot_ ] ) ); }

bptree_base::base_iterator &
bptree_base::base_iterator::operator++() noexcept
{
    BOOST_ASSERT_MSG( node_slot_, "Iterator at end: not incrementable" );
    auto & node{ this->node() };
    BOOST_ASSUME( node.num_vals >= 1 );
    if ( ++value_offset_ == node.num_vals ) [[ unlikely ]] {
        // implicitly becomes an end iterator when node.next is 'null'
        node_slot_    = node.next;
        value_offset_ = 0;
    }
    return *this;
}

bool bptree_base::base_iterator::operator==( base_iterator const & other ) const noexcept
{
    BOOST_ASSERT_MSG( &this->nodes_[ 0 ] == &other.nodes_[ 0 ], "Comparing iterators from different containers" );
    return ( this->node_slot_ == other.node_slot_ ) && ( this->value_offset_ == other.value_offset_ );
}


bptree_base::base_random_access_iterator &
bptree_base::base_random_access_iterator::operator+=( difference_type const n ) noexcept
{
    if ( n < 0 )
    {
        auto const un{ static_cast<size_type>( -n ) };
        BOOST_ASSUME( un < index_ );
        auto const absolute_pos{ index_ - un };
        node_slot_    = leaves_start_;
        value_offset_ = 0;
        index_        = 0;
        return *this += static_cast<difference_type>( absolute_pos );
    }

    BOOST_ASSERT_MSG( node_slot_, "Iterator at end: not incrementable" );
    auto un{ static_cast<size_type>( n ) };
    for ( ;; ) {
        auto & node{ this->node() };
        auto const available_offset{ static_cast<size_type>( node.num_vals - 1 - value_offset_ ) };
        if ( available_offset >= un ) [[ likely ]] {
            value_offset_ += static_cast<node_size_type>( un );
            break;
        } else {
            un -= available_offset;
            node_slot_    = node.next;
            value_offset_ = 0;
            BOOST_ASSERT_MSG( node_slot_ || un == 0, "Incrementing out of bounds" );
        }
    }
    index_ += static_cast<size_type>( n );
    return *this;
}


void bptree_base::swap( bptree_base & other ) noexcept
{
    using std::swap;
    swap( this->nodes_, other.nodes_ );
}

[[ gnu::cold ]]
bptree_base::linked_node_header &
bptree_base::create_root()
{
    BOOST_ASSUME( !hdr().root_  );
    BOOST_ASSUME( !hdr().depth_ );
    BOOST_ASSUME( !hdr().size_  );
    // the initial/'lone' root node is a leaf node
    auto & root{ static_cast<linked_node_header &>( static_cast<node_header &>( new_node() ) ) };
    auto & hdr { this->hdr() };
    BOOST_ASSUME( root.num_vals == 0 );
    root.num_vals  = 1;
    root.next      = {};
    hdr.root_      = slot_of( root );
    hdr.leaves_    = hdr.root_;
    hdr.depth_     = 1;
    hdr.size_      = 1;
    return root;
}

bptree_base::depth_t bptree_base::   leaf_level() const noexcept { BOOST_ASSUME( hdr().depth_ ); return hdr().depth_ - 1; }
             bool    bptree_base::is_leaf_level( depth_t const level ) const noexcept { return level == leaf_level(); }

void bptree_base::free( node_header & node ) noexcept
{
    BOOST_ASSUME( node.num_vals == 0 );
    auto & free_list{ hdr().free_list_ };
    auto & free_node{ static_cast<struct free_node &>( node ) };
    if ( free_list ) free_node.next = free_list;
    else             free_node.next = {};
    free_list = slot_of( free_node );
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
    auto & free_list{ hdr().free_list_ };
    if ( free_list )
    {
        auto & cached_node{ node<free_node>( free_list ) };
        BOOST_ASSUME( cached_node.num_vals == 0 );
        free_list = cached_node.next;
        cached_node.next = {};
        return as<node_placeholder>( cached_node );
    }
    auto & new_node{ nodes_.emplace_back() };
    BOOST_ASSUME( new_node.num_vals == 0 );
    return new_node;
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
