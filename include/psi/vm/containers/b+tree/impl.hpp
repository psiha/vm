#pragma once
////////////////////////////////////////////////////////////////////////////////
///
/// The comparator-dependent half: search, and the insert/erase/merge
/// entry points that have to search before they can place anything.
///
////////////////////////////////////////////////////////////////////////////////

#include "keyed_base.hpp"

#include <psi/vm/containers/komparator.hpp>
#include <psi/vm/containers/lookup.hpp>

#include <psi/build/disable_warnings.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>
#include <boost/stl_interfaces/sequence_container_interface.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <ranges>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

PSI_WARNING_DISABLE_PUSH()
PSI_WARNING_MSVC_DISABLE( 4127 ) // conditional expression is constant
PSI_WARNING_MSVC_DISABLE( 5030 ) // unrecognized attribute



////////////////////////////////////////////////////////////////////////////////
// \class bp_tree_impl
////////////////////////////////////////////////////////////////////////////////

template <typename Key, typename Comparator = std::less<>>
class bp_tree_impl
    :
    public  bptree_base_wkey<Key>,
#if 0 // reexamining...
    public  boost::stl_interfaces::sequence_container_interface<bp_tree_impl<Key, Comparator>, boost::stl_interfaces::element_layout::discontiguous>,
#endif
    protected Komparator<Comparator>
{
protected:
    using base = bptree_base_wkey<Key>;

    using Komp = Komparator<Comparator>;

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
    using find_pos       = base::find_pos;
    using iter_pos       = base::iter_pos;

    using bptree_base::as;
    using bptree_base::children;
    using bptree_base::keys;
    using bptree_base::key_at;
    using bptree_base::node;
    using bptree_base::num_chldrn;
    using bptree_base::shift_entries_left;
    using bptree_base::shift_entries_right;
    using bptree_base::lshift_entries;
    using bptree_base::rshift_entries;
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
    static constexpr auto transparent_comparator{ requires{ typename Comparator::is_transparent; } };

    using size_type       = base::size_type;
    using value_type      = base::value_type;
    using       pointer   = value_type       *;
    using const_pointer   = value_type const *;
    using       reference = value_type       &;
    using const_reference = value_type const &;
    using       iterator  = base::      iterator;
    using const_iterator  = base::const_iterator;

    using const_ra_iterator = std::basic_const_iterator<ra_iterator>;

    using       iter_pair = std::pair<      iterator,       iterator>;
    using const_iter_pair = std::pair<const_iterator, const_iterator>;

    using base::empty;
    using base::size;
    using base::clear;

    bp_tree_impl(                             ) noexcept = default;
    bp_tree_impl( Comparator const & comp     ) noexcept : Komp{ comp } {}
    bp_tree_impl( bp_tree_impl const & source ) : base{ source }, Komp{ static_cast<Komp const &>( source ) } {}
    bp_tree_impl( bp_tree_impl &&             ) noexcept = default;
    bp_tree_impl & operator=( bp_tree_impl && ) noexcept = default;

    [[ gnu::pure ]] const_iterator begin() const noexcept { return static_cast<iterator &&>( mutable_this().base::begin() ); }
    [[ gnu::pure ]] const_iterator   end() const noexcept { return static_cast<iterator &&>( mutable_this().base::  end() ); }

    [[ gnu::pure ]] auto random_access() const noexcept { return std::ranges::subrange{ ra_begin(), ra_end(), size() }; }
    [[ gnu::pure ]] const_ra_iterator ra_begin() const noexcept { return static_cast<ra_iterator &&>( mutable_this().base::ra_begin() ); }
    [[ gnu::pure ]] const_ra_iterator ra_end  () const noexcept { return static_cast<ra_iterator &&>( mutable_this().base::ra_end  () ); }

    // Forward-only lower_bound: returns the first element >= key, starting from pos.
    // Returns end() only when key > all elements.
    [[ nodiscard ]] const_iterator lower_bound_from( const_iterator const pos, LookupType<transparent_comparator, Key> auto const & key ) const noexcept { return lower_bound_from_impl( pos.base().pos(), pass_in_reg{ key } ); }
    [[ nodiscard ]] bool           contains        (                           LookupType<transparent_comparator, Key> auto const & key ) const noexcept { return contains_impl        (                   pass_in_reg{ key } ); }

    size_type merge( bp_tree_impl       && other, bool unique );
    size_type merge( bp_tree_impl const &  other, bool unique );

    void swap( bp_tree_impl & other ) noexcept { base::swap( other ); }

    using Komp::comp;
    using Komp::lt;
    using Komp::gt;
    using Komp::eq;
    using Komp::le;
    using Komp::ge;

    // UB if the comparator is changed in such a way as to invalidate the order of elements already in the container
    [[ nodiscard, gnu::pure ]] Comparator & mutable_comp() noexcept { return this->comp(); }

protected: // pass-in-reg public function overloads/impls
    bp_tree_impl & mutable_this() const noexcept { return const_cast<bp_tree_impl &>( *this ); }
    base         & mutable_base() const noexcept { return mutable_this(); }

    bool contains_impl( Reg auto const key, bool const unique ) const noexcept { return find_internal( key, unique ).first != nullptr; }

    [[ using gnu: pure, sysv_abi ]]
    const_iterator find_impl( Reg auto const key, bool const unique ) const noexcept
    {
        auto const find_result{ find_internal( key, unique ) };
        if ( find_result.first ) [[ likely ]]
            return base::make_iter( *find_result.first, find_result.second );

        return end();
    }

    // Forward-only lower_bound: returns the first element >= key starting from pos.
    // Reuses find_from() which already computes the insertion point — but instead
    // of discarding non-exact positions, returns the lower_bound iterator.
    //
    // Contract (FORWARD-ONLY): the key must be ≥ the key currently at `pos`.
    // I.e. either `pos` is past-the-end of its leaf (value_offset == num_vals,
    // nothing to compare against), or `key ≥ keys(leaf(pos.node))[pos.value_offset]`.
    // Passing a smaller key would require walking *backward* in the leaf — a case
    // `find_from`'s one-leaf fast path doesn't support and which later trips the
    // `starting_leaf != containing_leaf || pos.pos == num_vals` assumption inside
    // `find_from` with no caller context.
    [[ using gnu: pure, sysv_abi ]]
    const_iterator lower_bound_from_impl( iter_pos const pos, Reg auto const key ) const noexcept
    {
        BOOST_ASSUME( !empty() );
        auto const & starting_leaf{ leaf( pos.node ) };
        BOOST_ASSERT_MSG
        (
            pos.value_offset == starting_leaf.num_vals ||
            !lt( key, keys( starting_leaf )[ pos.value_offset ] ),
            "lower_bound_from() contract violation: 'key' is smaller than the key at the given position"
        );
        auto const [p_leaf, next_pos]{ find_from( starting_leaf, pos.value_offset, key ) };
        // next_pos.pos is the insertion point (first element >= key) regardless of exact_find
        if ( next_pos.pos < p_leaf->num_vals ) {
            return base::make_iter( *p_leaf, next_pos.pos );
        }
        // Key is past the end of the found leaf — advance to next leaf
        if ( p_leaf->right ) [[ unlikely ]] {
            return base::make_iter( base::leaf( p_leaf->right ), node_size_type{ 0 } );
        }
        return end();
    }

    [[ using gnu: pure, sysv_abi ]]
    const_iterator lower_bound_impl( Reg auto const key, bool const unique ) const noexcept
    {
        if ( !empty() ) [[ likely ]]
        {
            auto const location{ mutable_this().find_nodes_for( key, unique ) };
            // find_nodes_for() returns an insertion point, which can be one
            // pass the end of a node (like an end iterator, indicating an
            // append/push_back position) but iterators do not support this
            // (have to be initialized with a valid position for the increment
            // operator to work) - so such a position is expressed as the first
            // value of the following leaf, or, past the last leaf, as end()
            // (which is the last leaf at its num_vals offset, not a null node).
            if ( location.leaf_offset.pos == location.leaf.num_vals ) [[ unlikely ]] {
                BOOST_ASSUME( !location.leaf_offset.exact_find );
                if ( BOOST_UNLIKELY( !location.leaf.right ) )
                    return end();
                return base::make_iter( location.leaf.right, node_size_type{ 0 } );
            }
            return base::make_iter( location );
        }

        return end();
    }

    std::pair<const_iterator, bool> insert_impl( Reg auto const v, bool const unique )
    {
        if ( empty() )
        {
            auto & new_root{ static_cast<leaf_node &>( base::create_root() ) };
            BOOST_ASSUME( new_root.num_vals == 1 );
            key_at( new_root, 0 ) = v;
            return { begin(), true };
        }

        auto const [p_leaf, pos]{ find_insertion_point( v, unique ) };
        if ( pos.exact_find ) [[ unlikely ]] {
            BOOST_ASSUME( unique );
            return { base::make_iter( *p_leaf, pos.pos ), false };
        }
        auto const insert_pos_next{ base::insert( *p_leaf, pos.pos, Key{ v }, { /*insertion starts from leaves which do not have children*/ } ) };
        ++this->hdr().size_;
        return { base::make_iter( insert_pos_next ), true };
    }

    const_iterator insert_impl( const_iterator const pos_hint, Reg auto const v, [[ maybe_unused ]] bool const unique )
    {
        // yes, for starters, generic 'hint as just a hint' is not supported:
        // the hint has to be the exact resulting position, i.e. lower_bound(v).
        // end() is a valid hint - it is the append position (the last leaf at
        // its num_vals offset) and is what lower_bound() yields for a value
        // greater than every value in the tree.
        BOOST_ASSUME( !empty() );
        BOOST_ASSERT_MSG( ( pos_hint == end()   ) || lt( v, *pos_hint            ), "Invalid insertion hint" );
        BOOST_ASSERT_MSG( ( pos_hint == begin() ) || gt( v, *std::prev( pos_hint ) ) || !unique, "Invalid insertion hint" );

        auto const [hint_slot, hint_slot_offset]{ pos_hint.base().pos() };
        auto & hint_leaf{ leaf( hint_slot ) };
        if ( hint_slot_offset == 0 ) [[ unlikely ]] {
            base::update_separator( hint_leaf, v );
        }
        auto const insert_pos_next{ base::insert( hint_leaf, hint_slot_offset, Key{ v }, {} ) };
        ++this->hdr().size_;
        // the value landed at the hinted position, which now denotes the
        // inserted value rather than the one it was taken from - except for an
        // append, where the hint was end() and the inserted value sits at the
        // offset the hint carried.
        return base::make_iter( insert_pos_next );
    }

    [[ using gnu: pure, sysv_abi ]]
    std::pair<leaf_node *, node_size_type> find_internal( Reg auto const key, bool const unique ) const noexcept
    {
        if ( !empty() ) [[ likely ]]
        {
            auto const location{ mutable_this().find_nodes_for( key, unique ) };
            if ( location.leaf_offset.exact_find ) [[ likely ]] {
                return { &location.leaf, location.leaf_offset.pos };
            }
        }
        return {};
    }

private:
    // lower_bound find >limited to/within a node<
    // The capacity is the searched node's own, not the leaf's: it bounds
    // num_vals and it selects the intra-node search. Inner and leaf nodes hold
    // a different number of entries the moment a leaf carries anything besides
    // the key (a map), and for a set the two still differ by the child slots.
    template <node_size_type maximum_values>
    [[ using gnu: pure, hot, noinline, sysv_abi, leaf ]]
    static find_pos lower_bound( Key const keys[], node_size_type const num_vals, Reg auto const key, pass_in_reg<Comparator> const comparator ) noexcept
    {
        // TODO branchless binary search, Alexandrescu's TLC,
        // https://orlp.net/blog/bitwise-binary-search
        // https://algorithmica.org/en/eytzinger
        // FAST: Fast Architecture Sensitive Tree Search on Modern CPUs and GPUs http://kaldewey.com/pubs/FAST__SIGMOD10.pdf
        // ...
        BOOST_ASSUME( num_vals >  0              );
        BOOST_ASSUME( num_vals <= maximum_values );
        Comparator const & __restrict comp( comparator );
        decltype( auto ) value{ prefetch( comp, key ) };
        auto const pos_iter
        {
            use_linear_search_for_sorted_array<Comparator, Key, maximum_values>
                ? linear_lower_bound( &keys[ 0 ], &keys[ num_vals ], value, make_trivially_copyable_predicate( comp ) )
                :   std::lower_bound( &keys[ 0 ], &keys[ num_vals ], value, make_trivially_copyable_predicate( comp ) )
        };
        auto const pos_idx   { static_cast<node_size_type>( std::distance( &keys[ 0 ], pos_iter ) ) };
        auto const exact_find{ ( pos_idx != num_vals ) && !comp( value, keys[ pos_idx ] ) };
        return { pos_idx, exact_find };
    }
    template <node_size_type maximum_values>
    find_pos lower_bound( Key const keys[], node_size_type const num_vals, Reg auto const value ) const noexcept { return lower_bound<maximum_values>( keys, num_vals, value, pass_in_reg{ comp() } ); }
    find_pos lower_bound( auto const & node, auto const & value ) const noexcept { return lower_bound<bptree_base::node_capacity<decltype( node )>>( node.keys, node.num_vals, pass_in_reg{ value } ); }
    [[ using gnu: pure, hot, sysv_abi ]]
    find_pos lower_bound( auto const & node, node_size_type const offset, Reg auto const value ) const noexcept
    {
        BOOST_ASSUME( offset < node.num_vals );
        auto result{ lower_bound<bptree_base::node_capacity<decltype( node )>>( &key_at( node, offset ), node.num_vals - offset, value ) };
        result.pos += offset;
        return result;
    }

protected:
    size_type replace_keys_inplace( std::span<Key const> old_keys, std::span<Key const> new_keys, bool unique ) noexcept;
    template <bool require_exact_equality = false>
    size_type erase_sorted_impl   ( std::span<Key const> keys_to_remove                         , bool unique ) noexcept;
    size_type erase_sorted        ( std::span<Key const> keys_to_remove                         , bool unique ) noexcept { return erase_sorted_impl<false>( keys_to_remove, unique ); }
    // Like erase_sorted, but requires exact key equality (==) not just equivalence by comparator.
    // Useful when keys are indirect indices and comparison is by looked-up values.
    size_type erase_sorted_exact  ( std::span<Key const> keys_to_remove                         , bool unique ) noexcept { return erase_sorted_impl<true >( keys_to_remove, unique ); }

    // upper_bound find >limited to/within a node<
    template <node_size_type maximum_values>
    [[ using gnu: pure, hot, noinline, sysv_abi, leaf ]]
    static node_size_type upper_bound( Key const keys[], node_size_type const num_vals, Reg auto const key, pass_in_reg<Comparator> const comparator ) noexcept
    {
        BOOST_ASSUME( num_vals >  0              );
        BOOST_ASSUME( num_vals <= maximum_values );
        Comparator const & __restrict comp( comparator );
        decltype( auto ) value{ prefetch( comp, key ) };
        auto const pos_iter
        {
            use_linear_search_for_sorted_array<Comparator, Key, maximum_values>
                ? linear_upper_bound( &keys[ 0 ], &keys[ num_vals ], value, make_trivially_copyable_predicate( comp ) )
                :   std::upper_bound( &keys[ 0 ], &keys[ num_vals ], value, make_trivially_copyable_predicate( comp ) )
        };
        return static_cast<node_size_type>( std::distance( &keys[ 0 ], pos_iter ) );
    }
    template <node_size_type maximum_values>
    node_size_type upper_bound( Key const keys[], node_size_type const num_vals, Reg auto const value ) const noexcept { return upper_bound<maximum_values>( keys, num_vals, value, pass_in_reg{ comp() } ); }
    node_size_type upper_bound( auto const & node, auto const & value ) const noexcept { return upper_bound<bptree_base::node_capacity<decltype( node )>>( node.keys, node.num_vals, pass_in_reg{ value } ); }
    [[ using gnu: pure, hot, sysv_abi ]]
    node_size_type upper_bound( auto const & node, node_size_type const offset, Reg auto const value ) const noexcept
    {
        BOOST_ASSUME( offset < node.num_vals );
        return upper_bound<bptree_base::node_capacity<decltype( node )>>( &key_at( node, offset ), node.num_vals - offset, value ) + offset;
    }

    // upper_bound find >from a starting point, across nodes within the level/depth of the starting node<
    std::pair<iter_pos, size_type> upper_bound_across_nodes( auto const & node, node_size_type const offset, Reg auto const value ) const noexcept
    {
        // 'optimized' (simplified to linear across the leaves) for the
        // assumption/prevalence of shorter equal-range spans that will mostly
        // fit within a single node (making it not worth it to go up the tree
        // like find_from, the lower_bound version, does).
        auto * p_node{ &node };
        // extracted the initial call to upper_bound as it is the only one that
        // has to take the offset in to account (simplifies the loop slightly)
        auto pos  { upper_bound( *p_node, offset, value ) };
        auto count{ static_cast<size_type>( pos - offset ) };
        for ( ; ; )
        {
            if ( ( pos < p_node->num_vals ) || !p_node->right )
                break;
            p_node = &right( *p_node );
            pos    = upper_bound( *p_node, value );
            count += pos;
        }
        return { { slot_of( *p_node ), pos }, count };
    }


    [[ using gnu: pure, hot, sysv_abi, noinline ]]
    base::key_locations find_nodes_for( Reg auto const key, bool const nonuniques_span_across_nodes_check_not_needed ) noexcept
    {
        node_slot      separator_key_node;
        node_size_type separator_key_offset{};
        // A leaf (lone) root is implicitly handled by the loop condition:
        // if depth == 1 the loop is skipped entirely and the lone root is never
        // examined through an incorrectly typed reference.
        auto       current_node{ this->hdr().root_  };
        auto const depth       { this->hdr().depth_ };
        BOOST_ASSUME( depth >= 1 );
        for ( auto level{ 0 }; level < depth - 1; ++level )
        {
            auto const & node{ this->inner( current_node ) };
            auto [pos, exact_find]{ lower_bound( node, key ) };
            if ( exact_find ) [[ unlikely ]] // "most keys are in leaves"
            {
                // separator key - it also means we have to traverse to the right

                // In non unique instances it may happen that so many copies of
                // a key K are inserted that they spill into more than one leaf
                // (or even inner nodes in more extreme cases) - in case K
                // starts to appear later than from the beginning of the first
                // leaf (KL1), the parent will contain a separator key K that
                // would 'point' the downward search below to the right sibling
                // of KL1 (because the said right sibling contains K copies
                // again, from its beginning) - to blindly follow to the right
                // child/sibling would be a mistake as we would skip the first
                // K appearance in the left child/sibling (i.e. incorrect lower
                // bound behaviour).
                // This check requires extra memory access so we rather pay with
                // extra, predictable, branching through the added 
                // nonuniques_span_across_nodes_check_not_needed argument
                // (typically unique instances would set it to signal this
                // behaviour is not needed).
                PSI_WARNING_DISABLE_PUSH()
                PSI_WARNING_GCC_OR_CLANG_DISABLE( -Winvalid-offsetof )
                // At ( level == depth - 2 ) the child would already be a leaf,
                // however node layout/design guarantees that keys start at the
                // same offset regardless (only the capacity of the array
                // differs).
                static_assert( offsetof( inner_node, keys ) == offsetof( leaf_node, keys ) );
                PSI_WARNING_DISABLE_POP()
                if
                (
                    nonuniques_span_across_nodes_check_not_needed ||
                    lt( keys( this->leaf( node.children[ pos ] ) ).back(), key )
                ) [[ likely ]]
                {
                    // BOOST_ASSUME( !separator_key_node || !unique ); // exact_find may happen at most once (in unique trees :/)
                    separator_key_node   = current_node;
                    separator_key_offset = pos;
                    ++pos; // traverse to the right child
                }
            }
            current_node = node.children[ pos ];
        }
        auto & leaf{ this->leaf( current_node ) };
        return
        {
            leaf,
            BOOST_LIKELY( !separator_key_node ) ? lower_bound( leaf, key ) : find_pos{ 0, true }, // short circuit since we know separator keys only exist for first keys
            separator_key_offset,
            separator_key_node
        };
    }
    auto find_nodes_for( Key const & key, bool const unique ) noexcept { return find_nodes_for<Key>( key, unique ); }

    using insertion_point_t = std::pair<leaf_node *, find_pos>;
    [[ using gnu: pure, hot, sysv_abi, noinline ]]
    insertion_point_t find_insertion_point( Reg auto const key, bool const unique ) const noexcept
    {
        if ( unique ) {
            auto const loc{ mutable_this().find_nodes_for( key, unique ) };
            return { &loc.leaf, loc.leaf_offset };
        } else {
            auto       p_node{ &this->template as<parent_node>( root() ) };
            auto const depth { this->hdr().depth_ };
            BOOST_ASSUME( depth >= 1 );
            for ( auto level{ 0 }; level < depth - 1; ++level )
            {
                auto const pos{ upper_bound( *p_node, key ) };
                p_node = &this->template node<parent_node>( p_node->children[ std::min( pos, p_node->num_vals ) ] );
            }
            auto & leaf{ base::template as<leaf_node>( *p_node ) };
            auto const leaf_pos{ upper_bound( leaf, key ) };
            return { const_cast<leaf_node *>( &leaf ), { leaf_pos, false } };
        }
    }

    insertion_point_t find_next_insertion_point( leaf_node const & starting_leaf, node_size_type const starting_leaf_offset, Reg auto const key, bool const unique ) const noexcept
    {
        if ( unique ) return find_from          ( starting_leaf, starting_leaf_offset, key );
        else          return find_from_nonunique( starting_leaf, starting_leaf_offset, key );
    }

    template <comparator_erasure Erasure = Komp::erasure>
    size_type insert( typename base::bulk_copied_input, bool unique );

    // dedup_source: when true and unique, inline-deduplicates the presorted
    // input at each copy/merge point (empty-tree bulk, append, interleaved merge).
    template <bool dedup_source = false>
    size_type insert_presorted_impl( std::span<Key const> presorted_input, bool unique );

    // Handles possibly non-unique presorted input (deduplicates inline for unique trees).
    size_type insert_presorted       ( std::span<Key const> input, bool unique ) { return insert_presorted_impl<true >( input, unique ); }
    // Assumes all keys in presorted_input are distinct (faster for unique trees).
    size_type insert_presorted_unique( std::span<Key const> input, bool unique )
    {
        BOOST_ASSERT( std::ranges::adjacent_find( input, [this]( auto const & a, auto const & b ) noexcept { return this->eq( a, b ); } ) == input.end() );
        return insert_presorted_impl<false>( input, unique );
    }

#if !( defined( _MSC_VER ) && !defined( __clang__ ) )
    // ambiguous call w/ VS 17.11, 17.12
    void verify( auto const & node ) const noexcept
    {
        BOOST_ASSERT( std::ranges::is_sorted( keys( node ), comp() ) );
        base::verify( node );
    }
#endif
    using base::verify_min_max;

private:
    // Shared tree-climbing middle used by find_from and find_from_nonunique.
    // Precondition: key > starting_leaf.back() AND starting_leaf.right != null.
    // Climbs the parent chain until a node whose key range contains key, then
    // descends back to the containing leaf. Returns that leaf for the caller to
    // apply its own lower_bound / upper_bound epilogue.
    leaf_node & find_from_climb( leaf_node const & starting_leaf, Reg auto const key ) const noexcept
    {
        // Key in tree but not in starting leaf - go up the tree:
        auto const * prnt{ &parent( starting_leaf ) };
        auto         parent_offset{ starting_leaf.tail.parent_child_idx };
        // Children are right shifted (WRT the keys array which has one less
        // element) - those which have a key on the same/corresponding index
        // should have a strictily less-than starting key value than the parent
        // (separator key).
        BOOST_ASSUME( ( parent_offset == prnt->num_vals ) || lt( key_at( starting_leaf, 0 ), key_at( *prnt, parent_offset ) ) );
        auto const depth{ this->hdr().depth_ }; BOOST_ASSUME( depth >= 1 );
        auto       level{ depth - 1 };
        while ( lt( keys( *prnt ).back(), key ) )
        {
            if ( level == 1 ) [[ unlikely ]]
            {
                // reached the root
                BOOST_ASSUME( !prnt->parent );
                // the case where the key does not exist at all is handled at
                // the beginning so the only case where parent_offset could
                // point to the end is on intermediate inner/parent nodes (when
                // depth is more then 2 levels)
                // (and this has to be handled explicitly because otherwise the
                // lower_bound call below would get fed empty input which
                // it does not support)
                BOOST_ASSUME( depth > 2 || ( parent_offset < prnt->num_vals ) );
                parent_offset = std::min( parent_offset, node_size_type( prnt->num_vals - 1 ) );
                break;
            }
            parent_offset = prnt->tail.parent_child_idx;
            prnt          = &parent( *prnt );
            --level;
        }
        BOOST_ASSUME( parent_offset < prnt->num_vals );
        // descend to the leaf containing the key
        for ( ; level < depth; ++level )
        {
            auto [pos, exact_find]{ lower_bound( *prnt, parent_offset, key ) };
            // prior to Dec 11th 2025 the assumption below always held
            // (inexplicably so) and then after the addition of the
            // insert_presorted method it started failing (when this function
            // was called from within it) - TODO investigate
            //BOOST_ASSUME( !exact_find );
            pos += exact_find; // traverse to the right child for separator keys
            prnt = &base::inner( children( *prnt )[ pos ] );
            parent_offset = 0;
        }
        BOOST_ASSUME( parent_offset == 0 );
        return const_cast<leaf_node &>( this->template as<leaf_node>( *prnt ) );
    }

    // Forward-only insertion-point search for unique trees (lower_bound semantics).
    // Exploits sorted-input invariant to avoid repeated root-to-leaf traversals.
    // starting_leaf_offset must be <= num_vals. When == num_vals (past-the-end,
    // e.g. after merge fills a leaf), the caller guarantees key > back() so the
    // in-leaf fast path is never taken and no OOB access occurs.
    insertion_point_t find_from( leaf_node const & starting_leaf, node_size_type const starting_leaf_offset, Reg auto const key ) const noexcept
    {
        BOOST_ASSUME( starting_leaf_offset <= starting_leaf.num_vals );
        if ( le( key, keys( starting_leaf ).back() ) )
        {
            // Fast path: check the immediate next element before binary search.
            // In cursor-walk patterns the next key is almost always at offset.
            // When offset == num_vals the le() guard above must be false (caller
            // guarantees key > back()), so we never reach here with an OOB offset.
            BOOST_ASSUME( starting_leaf_offset < starting_leaf.num_vals );
            auto const leaf_keys{ keys( starting_leaf ) };
            if ( le( key, leaf_keys[ starting_leaf_offset ] ) ) // key <= keys[offset]
                return { const_cast<leaf_node *>( &starting_leaf ), find_pos{ starting_leaf_offset, eq( key, leaf_keys[ starting_leaf_offset ] ) } };
            // Fall back to binary search within the leaf, skipping the tested slot
            auto const search_from{ static_cast<node_size_type>( starting_leaf_offset + 1 ) };
            auto const pos{ lower_bound( starting_leaf, search_from, key ) };
            BOOST_ASSUME( pos.pos != starting_leaf.num_vals );
            BOOST_ASSUME( pos.pos >= starting_leaf_offset   );
            return { const_cast<leaf_node *>( &starting_leaf ), pos };
        }

        if ( !starting_leaf.right ) [[ unlikely ]] // we are at the end of the tree/leaf level: key not present at all
            return { const_cast<leaf_node *>( &starting_leaf ), find_pos{ starting_leaf.num_vals, false } };

        // Right-sibling fast path: should work but needs investigation — crashes
        // in bp_tree.playground test (erase returns false → access violation).
        // Hypothesis: during bulk insert/merge the tree traversal path (up parent
        // chain, back down) correctly accounts for structural changes (splits,
        // pool reallocations) that the direct right-sibling jump bypasses.
#   if 0
        {
            auto const & right_leaf{ leaf( starting_leaf.right ) };
            if ( le( key, keys( right_leaf ).back() ) )
            {
                auto const pos{ lower_bound( right_leaf, 0, key ) };
                return { const_cast<leaf_node *>( &right_leaf ), pos };
            }
        }
#   endif

        auto const & containing_leaf{ find_from_climb( starting_leaf, key ) };
        auto const pos{ lower_bound( containing_leaf, key ) };
        BOOST_ASSUME
        (
            ( &starting_leaf != &containing_leaf  ) ||
            // the worst case: when the value falls between existing nodes we
            // will land on the starting node again - TODO insert a new node
            ( pos.pos == containing_leaf.num_vals )
        );
        return { const_cast<leaf_node *>( &containing_leaf ), pos };
    }

    // Forward-only insertion-point search for non-unique trees (upper_bound semantics).
    // Inserts after all equal keys; exact_find is always false.
    // starting_leaf_offset must be <= num_vals. When == num_vals (past-the-end,
    // e.g. after merge fills a leaf), the caller guarantees key > back() so the
    // in-leaf fast path is never taken and no OOB access occurs.
    insertion_point_t find_from_nonunique( leaf_node const & starting_leaf, node_size_type const starting_leaf_offset, Reg auto const key ) const noexcept
    {
        BOOST_ASSUME( starting_leaf_offset <= starting_leaf.num_vals );
        if ( le( key, keys( starting_leaf ).back() ) )
        {
            // When offset == num_vals the le() guard above must be false (caller
            // guarantees key > back()), so we never reach here with an OOB offset.
            BOOST_ASSUME( starting_leaf_offset < starting_leaf.num_vals );
            auto const leaf_keys{ keys( starting_leaf ) };
            // Quick-probe: if key < keys[offset], upper_bound stops here — no search needed.
            if ( lt( key, leaf_keys[ starting_leaf_offset ] ) )
                return { const_cast<leaf_node *>( &starting_leaf ), find_pos{ starting_leaf_offset, false } };
            auto const pos{ upper_bound( starting_leaf, starting_leaf_offset, key ) };
            return { const_cast<leaf_node *>( &starting_leaf ), find_pos{ pos, false } };
        }

        if ( !starting_leaf.right ) [[ unlikely ]] // we are at the end of the tree/leaf level
            return { const_cast<leaf_node *>( &starting_leaf ), find_pos{ starting_leaf.num_vals, false } };

        auto const & containing_leaf{ find_from_climb( starting_leaf, key ) };
        auto const pos{ upper_bound( containing_leaf, key ) };
        BOOST_ASSUME
        (
            ( &starting_leaf != &containing_leaf  ) ||
            ( pos == containing_leaf.num_vals )
        );
        return { const_cast<leaf_node *>( &containing_leaf ), find_pos{ pos, false } };
    }

    // bulk insert helper: merge a new, presorted leaf into an existing leaf
    auto merge
    (
        leaf_node const & source, node_size_type source_offset,
        leaf_node       & target, node_size_type target_offset,
        bool unique
    ) noexcept;
    auto merge
    (
        Key const src_keys[], node_size_type input_length,
        leaf_node & target  , node_size_type target_offset,
        bool unique, bool dedup_source = false
    ) noexcept;

    node_size_type merge_interleaved_values
    (
        Key const source0[], node_size_type source0_size,
        Key const source1[], node_size_type source1_size,
        Key       target [], bool unique, bool dedup_source = false
    ) const noexcept;

}; // class bp_tree_impl



//--------------------------------------------------------------------------
// Replace keys in-place (used for row index updates after compaction).
//
// When row indices change but the sort order (determined by the comparator
// looking up coordinate values) remains the same, this method replaces old
// row indices with new ones using efficient tree traversal.
//
// Preconditions:
// - old_keys and new_keys have the same size
// - Both spans are sorted by the tree's comparator ordering
// - Each old_keys[i] exists in the tree
// - Each pair (old_keys[i], new_keys[i]) compares equivalent (same position)
//
// Returns: number of keys replaced (i.e. old_keys.size())
//--------------------------------------------------------------------------

template <typename Key, typename Comparator>
bp_tree_impl<Key, Comparator>::size_type
bp_tree_impl<Key, Comparator>::replace_keys_inplace( std::span<Key const> const old_keys, std::span<Key const> const new_keys, bool const unique ) noexcept
{
    BOOST_ASSERT( old_keys.size() == new_keys.size() );
    BOOST_ASSERT( this   ->size() >= old_keys.size() || !this->all_bulk_erase_keys_must_exist );
    if ( old_keys.empty() || ( !this->all_bulk_erase_keys_must_exist && this->empty() ) )
        return 0;

    size_type replaced{ 0 };
    size_t    key_idx { 0 };

    // Find starting position for first key
    auto const location{ find_nodes_for( old_keys[ key_idx ], unique ) };
    if ( !location.leaf_offset.exact_find ) [[ unlikely ]] {
        BOOST_ASSUME( !this->all_bulk_erase_keys_must_exist );
        return 0; // First key not found
    }

    auto p_leaf{ &location.leaf };
    auto offset{ location.leaf_offset.pos };

    while ( key_idx < old_keys.size() )
    {
        // Verify and replace the key at current position
        BOOST_ASSERT( key_at( *p_leaf, offset ) == old_keys[ key_idx ] );
        BOOST_ASSERT_MSG(
            eq( old_keys[ key_idx ], new_keys[ key_idx ] ),
            "Replacement key must compare equivalent to old key (same ordering position)"
        );
        key_at( *p_leaf, offset ) = new_keys[ key_idx ];
        if ( offset == 0 ) [[ unlikely ]] {
            this->update_separator( *p_leaf, new_keys[ key_idx ] );
        }
        p_leaf->mark_dirty();
        ++replaced;
        ++key_idx;

        if ( key_idx >= old_keys.size() )
            break;

        // Advance past the just-processed key. If we land at num_vals (past-the-end),
        // move to the right sibling so offset stays in-range for find_from's in-leaf
        // probe (replacement keys compare equivalent → key <= back() takes the fast path).
        auto next_offset{ static_cast<node_size_type>( offset + 1 ) };
        if ( next_offset == p_leaf->num_vals )
        {
            if ( !p_leaf->right ) [[ unlikely ]]
                break; // no more leaves
            p_leaf      = &this->leaf( p_leaf->right );
            next_offset = 0;
        }

        // Find next key using find_from (O(1) quick-probe + in-leaf binary search + tree climbing)
        auto const [next_leaf, next_pos]{ find_from( *p_leaf, next_offset, old_keys[ key_idx ] ) };
        if ( !next_pos.exact_find ) [[ unlikely ]] {
            BOOST_ASSUME( !this->all_bulk_erase_keys_must_exist );
            break; // Key not found
        }

        p_leaf = next_leaf;
        offset = next_pos.pos;
    }

    BOOST_ASSERT( replaced == old_keys.size() );
    return replaced;
}

//--------------------------------------------------------------------------
// Bulk erase with pre-sorted keys.
//
// Removes multiple keys using find_nodes_for for the first key, then
// find_from for subsequent keys to avoid repeated tree traversals.
//
// When require_exact_equality is true, keys must match by operator== (not
// just comparator equivalence). This is useful when keys are indirect indices
// and comparison is done by looking up external values.
//
// Preconditions:
// - keys_to_remove is sorted by the tree's comparator ordering
//
// Returns: number of keys actually removed
//--------------------------------------------------------------------------

template <typename Key, typename Comparator>
template <bool require_exact_equality>
bp_tree_impl<Key, Comparator>::size_type
bp_tree_impl<Key, Comparator>::erase_sorted_impl( std::span<Key const> const keys_to_remove, bool const unique ) noexcept
{
    BOOST_ASSERT( this->size() >= keys_to_remove.size() || !this->all_bulk_erase_keys_must_exist );
    if ( keys_to_remove.empty() || ( !this->all_bulk_erase_keys_must_exist && this->empty() ) )
        return 0;

    BOOST_ASSERT( std::ranges::is_sorted( keys_to_remove, comp() ) );

    size_type erased_count{ 0 };
    size_t    key_idx     { 0 };

    // Helper: checks if the key at a given position is a valid match.
    // For equivalence-only mode, any comparator-equivalent key suffices.
    // For exact mode, also requires operator== identity.
    auto const is_match{ [&]( leaf_node const & lf, node_size_type pos, size_t kidx )
    {
        if constexpr ( require_exact_equality )
            return eq( key_at( lf, pos ), keys_to_remove[ kidx ] ) && key_at( lf, pos ) == keys_to_remove[ kidx ];
        else
            return eq( key_at( lf, pos ), keys_to_remove[ kidx ] );
    } };

    // Helper: scan keys_to_remove[key_idx..] using find_from until a match is found.
    // Uses forward-only cursor walk (keys_to_remove is sorted).
    auto const find_next_match{ [&]( leaf_node *& lf, node_size_type & off ) -> bool
    {
        while ( key_idx < keys_to_remove.size() )
        {
            auto [next_leaf, found_pos]{ find_from( *lf, off, keys_to_remove[ key_idx ] ) };
            if ( found_pos.exact_find && ( !require_exact_equality || key_at( *next_leaf, found_pos.pos ) == keys_to_remove[ key_idx ] ) )
            {
                lf  = next_leaf;
                off = found_pos.pos;
                return true;
            }
            lf  = next_leaf;
            off = found_pos.pos;
            ++key_idx;
        }
        return false;
    } };

    // Find first key's position with full tree traversal — use the returned
    // location (exact or not) as the starting point for find_from on subsequent keys.
    auto const first_location{ find_nodes_for( keys_to_remove[ 0 ], unique ) };
    auto * p_leaf{ &first_location.leaf };
    auto   offset{ first_location.leaf_offset.pos };
    if ( !first_location.leaf_offset.exact_find || ( require_exact_equality && key_at( *p_leaf, offset ) != keys_to_remove[ 0 ] ) )
    {
        // First key not found — try remaining keys using find_from from this position
        ++key_idx;
        if ( !find_next_match( p_leaf, offset ) )
            return 0;
    }

    while ( key_idx < keys_to_remove.size() )
    {
        // Verify and handle match at current position
        if constexpr ( require_exact_equality )
        {
            if ( key_at( *p_leaf, offset ) != keys_to_remove[ key_idx ] )
            {
                ++key_idx;
                if ( !find_next_match( p_leaf, offset ) )
                    break;
                continue;
            }
        }
        BOOST_ASSERT( eq( key_at( *p_leaf, offset ), keys_to_remove[ key_idx ] ) );

        // Update separator key if erasing at position 0
        if ( offset == 0 && p_leaf->num_vals > 1 ) [[ unlikely ]]
        {
            this->update_separator( *p_leaf, key_at( *p_leaf, 1 ) );
        }

        // Erase the key
        auto next_pos{ this->erase( *p_leaf, offset ) };
        ++erased_count;
        ++key_idx;

        if ( key_idx >= keys_to_remove.size() || this->empty() )
            break;

        // Find next key using find_from from current position
        if ( !next_pos.node ) [[ unlikely ]]
            break;

        p_leaf = &this->leaf( next_pos.node );
        offset = next_pos.value_offset;

        // Check if next key is at the current position
        if ( offset < p_leaf->num_vals && is_match( *p_leaf, offset, key_idx ) )
            continue;

        // Use find_from to locate the next key
        auto [next_leaf, found_pos]{ find_from( *p_leaf, offset, keys_to_remove[ key_idx ] ) };
        if ( !found_pos.exact_find || ( require_exact_equality && key_at( *next_leaf, found_pos.pos ) != keys_to_remove[ key_idx ] ) ) [[ unlikely ]]
        {
            if ( !find_next_match( p_leaf, offset ) )
                break;
            continue;
        }

        p_leaf = next_leaf;
        offset = found_pos.pos;
    }

    return erased_count;
}



template <typename Key, typename Comparator>
// bulk insert helper: merge a new, presorted leaf into an existing leaf
auto /*[ inserted_size, consumed_size, &target, next_tgt_offset ]*/
bp_tree_impl<Key, Comparator>::merge
(
    leaf_node const & source, node_size_type const source_offset,
    leaf_node       & target, node_size_type const target_offset,
    bool const unique
) /*?*/noexcept
{
    verify( source );
    BOOST_ASSUME( source_offset < source.num_vals );
    node_size_type const input_length( source.num_vals - source_offset );
    auto           const src_keys{ &key_at( source, source_offset ) };
    return merge( src_keys, input_length, target, target_offset, unique );
}

template <typename Key, typename Comparator>
auto /*[ inserted_size, consumed_size, &target, next_tgt_offset ]*/
bp_tree_impl<Key, Comparator>::merge
(
    Key const src_keys[], node_size_type const input_length,
    leaf_node & target  , node_size_type const target_offset,
    bool const unique, bool const dedup_source
) /*?*/noexcept
{
    BOOST_ASSUME( input_length > 0 );
    verify( target );
    node_size_type const available_space( target.max_values - target.num_vals ); // recheck: do we need a different value for roots here?
    auto & tgt_keys{ target.keys };
    BOOST_ASSERT
    (
        ( lower_bound( target, src_keys[ 0 ] ).pos == target_offset ) ||
        // in non unique contents lower_bound will return the first occurrence while target_offset should point past the last occurrence
        ( !unique && eq( tgt_keys[ target_offset - 1 ], src_keys[ 0 ] ) )
    );

    if ( !available_space ) [[ unlikely ]]
    {
        // if the first source key already exists simply 'consume' it and return
        // (in hope that a different target leaf, with available space, will be
        // selected next time)
        if (
            !unique && // skip duplicates only for unique instances
            ( target_offset != target.num_vals ) &&
            eq( key_at( target, target_offset ), src_keys[ 0 ] )
        ) [[ unlikely ]]
        {
            return std::make_tuple<node_size_type, node_size_type>( 0, 1, &target, target_offset );
        }
#   if 0 // a possibility for the merge overload which works with source nodes
        // support merging nodes from another tree instance:
        auto const source_slot{ base::is_my_node( source ) ? slot_of( source ) : node_slot{} };
        auto       [target_slot, nxt_tgt_offset]{ base::split_to_insert( target, target_offset, pass_rv_in_reg{ /*mrmlj*/Key{ src_keys[ 0 ] } }, {} ) };
        auto const & src{ source_slot ? leaf( source_slot ) : source };
        auto       & tgt{               leaf( target_slot )          };
        BOOST_ASSUME( nxt_tgt_offset <= tgt.num_vals );
        BOOST_ASSUME( nxt_tgt_offset >  0 );
        auto const nxt_src_offset{ static_cast<node_size_type>( source_offset + 1 ) };
        // next_tgt_offset returned by split_to_insert points to the
        // position in the target node that immediately follows the
        // position for the inserted src_keys[ 0 ] - IOW it need not be
        // the position for key_at( src, next_src_offset )
        if
        (
            ( nxt_tgt_offset != tgt.num_vals ) && // necessary check because find assumes non-empty input
            ( nxt_src_offset != src.num_vals ) && // possible edge case where there was actually only one key left in the source
            false                                 // not really worth it: the caller still has to call find on/for returns from all the other branches
        )
        {
            nxt_tgt_offset = lower_bound( tgt, nxt_tgt_offset, key_const_arg{ key_at( src, nxt_src_offset ) } ).pos;
        }
#   else
        auto [target_slot, nxt_tgt_offset]{ base::overflow_to_insert( target, target_offset, pass_rv_in_reg{ /*mrmlj*/Key{ src_keys[ 0 ] } }, {} ) };
        auto & tgt{ leaf( target_slot ) };
#   endif
        verify_min_max( tgt );
        return std::make_tuple<node_size_type, node_size_type>( 1, 1, &tgt, nxt_tgt_offset );
    }

    if ( target_offset == 0 ) [[ unlikely ]]
    {
        auto const & new_separator{ src_keys[ 0 ] };
        BOOST_ASSERT( lt( new_separator, tgt_keys[ 0 ] ) );
        base::update_separator( target, new_separator );
        // TODO rather simply insert the source leaf into the parent (if all of
        // its keys come before the first key in target)
    }

    auto copy_size{ std::min( input_length, available_space ) };
    // If there is an existing right sibling we must first check if the
    // source contains values beyond its separator key (and adjust the copy
    // size accordingly to maintain the sorted property).
    if ( target.right )
    {
        auto const & right_delimiter{ key_at( right( target ), 0 ) };
        // For unique trees: stop before any key >= right_delimiter (no duplicates allowed).
        // For non-unique trees: equal keys may span leaf boundaries, so stop only before
        // keys strictly greater than right_delimiter (equal keys go into the current leaf).
        node_size_type const cut_pos
        { unique
            ? lower_bound<leaf_node::max_values>( src_keys, copy_size, key_const_arg{ right_delimiter } ).pos
            : upper_bound<leaf_node::max_values>( src_keys, copy_size, key_const_arg{ right_delimiter } )
        };
        if ( cut_pos != copy_size )
        {
            BOOST_ASSUME( cut_pos > 0         );
            BOOST_ASSUME( cut_pos < copy_size );
            copy_size = cut_pos;
        }
    }

    auto & tgt_size{ target.num_vals };
    node_size_type inserted_size;
    node_size_type next_tgt_offset;
    if ( target_offset == tgt_size ) // a simple append
    {
        if ( dedup_source && unique )
        {
            auto const out{ std::unique_copy( src_keys, src_keys + copy_size, &tgt_keys[ target_offset ],
                [this]( auto const & a, auto const & b ) noexcept { return this->eq( a, b ); } ) };
            inserted_size = static_cast<node_size_type>( out - &tgt_keys[ target_offset ] );
        }
        else
        {
            std::copy_n( src_keys, copy_size, &tgt_keys[ target_offset ] );
            inserted_size = copy_size;
        }
        tgt_size        += inserted_size;
        next_tgt_offset  = tgt_size;
    }
    else
    {
        BOOST_ASSUME( tgt_size + copy_size <= leaf_node::max_values );
        // make room for merge: move existing values (beyond the insertion/merge
        // point) to the end of the buffer
        std::move_backward( &tgt_keys[ target_offset ], &tgt_keys[ tgt_size ], &tgt_keys[ tgt_size + copy_size ] );
        auto const new_tgt_size{ target_offset + merge_interleaved_values
        (
            &src_keys[ 0                         ], copy_size,
            &tgt_keys[ target_offset + copy_size ], tgt_size - target_offset,
            &tgt_keys[ target_offset             ], unique, dedup_source
        ) };
        inserted_size   = static_cast<node_size_type>( new_tgt_size - tgt_size );
        tgt_size        = static_cast<node_size_type>( new_tgt_size            );
        next_tgt_offset = target_offset + inserted_size;
    }
    target.mark_dirty();
    if ( !target.is_root() )
        verify_min_max( target );
    BOOST_ASSUME( inserted_size <= copy_size );
    return std::make_tuple( inserted_size, copy_size, &target, next_tgt_offset );
}

template <typename Key, typename Comparator>
bp_tree_impl<Key, Comparator>::node_size_type
bp_tree_impl<Key, Comparator>::merge_interleaved_values
(
    Key const source0[], node_size_type const source0_size,
    Key const source1[], node_size_type const source1_size,
    Key       target [], bool const unique, bool const dedup_source
) const noexcept
{
    node_size_type const input_size( source0_size + source1_size );
    if ( unique )
    {
        if ( dedup_source )
        {
            // Like std::set_union but also removes consecutive duplicates
            // within source0 (the presorted input may contain dupes).
            // source1 (existing tree keys) is already unique.
            auto       * out    { target };
            auto const * s0     { source0 }, * const s0_end{ &source0[ source0_size ] };
            auto const * s1     { source1 }, * const s1_end{ &source1[ source1_size ] };
            while ( s0 != s0_end && s1 != s1_end )
            {
                if ( lt( *s1, *s0 ) )
                {
                    *out++ = *s1++;
                }
                else if ( lt( *s0, *s1 ) )
                {
                    *out++ = *s0++;
                    while ( s0 != s0_end && eq( *s0, out[ -1 ] ) ) ++s0;
                }
                else // equivalent
                {
                    *out++ = *s0++;
                    ++s1;
                    while ( s0 != s0_end && eq( *s0, out[ -1 ] ) ) ++s0;
                }
            }
            while ( s0 != s0_end )
            {
                *out++ = *s0++;
                while ( s0 != s0_end && eq( *s0, out[ -1 ] ) ) ++s0;
            }
            while ( s1 != s1_end )
                *out++ = *s1++;
            auto const merged_size{ static_cast<node_size_type>( out - target ) };
            BOOST_ASSUME( merged_size <= input_size );
            return merged_size;
        }
        else
        {
            auto const out_pos{ std::set_union( source0, &source0[ source0_size ], source1, &source1[ source1_size ], target, comp() ) };
            auto const merged_size{ static_cast<node_size_type>( out_pos - target ) };
            BOOST_ASSUME( merged_size <= input_size );
            return merged_size;
        }
    }
    else
    {
        auto const out_pos{ std::merge( source0, &source0[ source0_size ], source1, &source1[ source1_size ], target, comp() ) };
        BOOST_ASSUME( out_pos == target + input_size );
        return input_size;
    }
}


template <typename Key, typename Comparator>
template <comparator_erasure Erasure>
bp_tree_impl<Key, Comparator>::size_type
bp_tree_impl<Key, Comparator>::insert( typename base::bulk_copied_input input, bool const unique )
{
    // https://www.sciencedirect.com/science/article/abs/pii/S0020025502002025 On batch-constructing B+-trees: algorithm and its performance
    // https://www.vldb.org/conf/2001/P461.pdf An Evaluation of Generic Bulk Loading Techniques
    // https://stackoverflow.com/questions/15996319/is-there-any-algorithm-for-bulk-loading-in-b-tree

    if ( input.size == 0 )
        return 0;

    auto const begin_leaf{ input.begin };
    auto       end_pos   { input.end   };
    auto       total_size{ input.size  };
    {
        // use specialized/optimized iterators (that can assume all nodes are
        // full)
        typename base::ra_full_node_iterator const sort_begin{ input.nodes.data(), 0          };
        typename base::ra_full_node_iterator const sort_end  { input.nodes.data(), total_size };
        this->template sort<Erasure>( sort_begin, sort_end );
        // Collapse keys equivalent to each other WITHIN the input - nothing
        // downstream does it. The empty-tree shortcut below adopts the input as
        // the tree verbatim, and the merge/bulk-append paths carry whole
        // presorted runs across rather than looking every key up; both leave a
        // unique tree holding equivalent keys and report all of them as
        // inserted. Doing it here, once, on the just-sorted input, is what
        // makes every one of those paths correct - and it is the same thing
        // insert_presorted has always done for its own (contiguous) input.
        if ( unique ) {
            auto const deduped_end
            {
                std::unique( sort_begin, sort_end, [this]( auto const & left, auto const & right ) noexcept
                    { return this->eq( left, right ); } )
            };
            auto const deduped_size{ static_cast<size_type>( deduped_end - sort_begin ) };
            if ( deduped_size != total_size ) {
                end_pos    = base::shrink_bulk_copied_input( input.nodes, total_size, deduped_size );
                total_size = deduped_size;
            }
        }
        input.nodes.clear();
    }

    if ( empty() )
    {
        base::bulk_insert_into_empty( begin_leaf, end_pos, total_size );
        BOOST_ASSUME( this->hdr().size_ == total_size );
        return total_size;
    }

    ra_iterator p_new_keys{ *this, { begin_leaf, 0 }, 0 };
    BOOST_ASSERT( p_new_keys.absolute_offset() == 0 );
    auto [source_slot, source_slot_offset]{ p_new_keys.pos() };
    auto src_leaf{ &leaf( source_slot ) };

    auto [tgt_leaf, tgt_leaf_next_pos]{ find_insertion_point( *p_new_keys, unique ) };

    size_type inserted{ 0 };
    for ( ;; )
    {
        BOOST_ASSUME( source_slot_offset < src_leaf->num_vals );
        // skip preexisting values for unique containers
        if ( tgt_leaf_next_pos.exact_find ) [[ unlikely ]]
        {
            BOOST_ASSUME( unique );
            // in case of many consecutive duplicates this devolves
            // pathologically into basically a loop of single-element insertions
            // - very slow - but at least it is documented that non-unique
            // insertions are "handled but not optimally so" - should probably
            // be relatively easy to fix given that the internal merge used
            // below supports this case (maybe needs a modification to allow for
            // "all duplicates"/no insertions case)
            ++p_new_keys;
        }
        else
        // if we have reached the end of the rightmost leaf simply perform a
        // bulk_append
        if ( ( tgt_leaf_next_pos.pos == tgt_leaf->num_vals ) && !tgt_leaf->right )
        {
            auto const so_far_consumed{ p_new_keys.absolute_offset() };
            BOOST_ASSUME( so_far_consumed < total_size );
            // at this point we know that all the remaining items will in fact
            // be inserted (even for unique instances) since we are inserting
            // right of/after all the existing data/items - so precalculate the
            // inserted count - before all of the horrendous ifology below
            inserted += static_cast<size_type>( total_size - so_far_consumed );
            // before proceeding with the bulk_append we have to:
            // - prepare the current src_leaf (so that it does not have a hole
            //   at the beginning)
            shift_entries_left( *src_leaf, 0, src_leaf->num_vals, source_slot_offset );
            src_leaf->num_vals -= source_slot_offset;
            // - link/append it to existing leaves
            base::link( *tgt_leaf, *src_leaf );
            // - fill it up if it is underflowed to make it a valid node (either
            //   by merging with the left sibling (tgt_leaf) or 'borrowing' from
            //   the next src leaf, the right sibling)
            // (yes, in case src_leaf is really incomplete, the shift_left above
            // is theoretically redundant, i.e. could be merged with the shifts/
            // copies in append_and_free or bulk_append_fill_leaf_if_incomplete)
            if ( ( tgt_leaf->num_vals + src_leaf->num_vals ) <= tgt_leaf->max_values ) {
                auto const src_size{ src_leaf->num_vals };
                base::append_and_free( *tgt_leaf, *src_leaf );
                if ( !tgt_leaf->is_root() )
                    verify_min_max( *tgt_leaf );
                if ( !tgt_leaf->right ) {
                    BOOST_ASSUME( so_far_consumed + src_size == total_size );
                    break; // to the trailing size_ update & return
                }
                src_leaf = &right( *tgt_leaf );
            }
            // yes - an else if will not do here because of yet another edge
            // case: when exactly two source leaves remain _and_ the one before
            // last is merged/consumed in the if above _and_ the last remaining
            // one is incomplete
            auto const fill_result{ base::bulk_append_fill_leaf_if_incomplete( src_leaf ) };
            if ( fill_result == base::incomplete_resolution::merged_and_freed ) {
                BOOST_ASSUME( tgt_leaf == src_leaf );
                if ( !tgt_leaf->right ) [[ likely ]] {
                    break; // to the trailing size_ update & return
                } else {
                    // we should actually never reach here: since
                    // bulk_copied_input is generated to have all nodes full
                    // (except possibly the last one)
                    src_leaf = &right( *tgt_leaf );
                    BOOST_UNREACHABLE();
                }
            } else
            if (
                ( fill_result == base::incomplete_resolution::not_incomplete ) &&
                ( tgt_leaf->num_vals < tgt_leaf->min_values ) // in case tgt_leaf is the root it too could be incomplete
            ) { [[ unlikely ]]
                BOOST_ASSUME( tgt_leaf->is_root() );
                node_size_type const missing_keys( tgt_leaf->min_values - tgt_leaf->num_vals );
                BOOST_ASSUME( tgt_leaf->num_vals + src_leaf->num_vals >= leaf_node::min_values * 2 );
                this->move_entries( *src_leaf, 0, missing_keys, *tgt_leaf, tgt_leaf->num_vals );
                shift_entries_left( *src_leaf, 0, src_leaf->num_vals, missing_keys );
                tgt_leaf->num_vals += missing_keys;
                src_leaf->num_vals -= missing_keys;
                tgt_leaf->mark_dirty();
                src_leaf->mark_dirty();
            }
            verify_min_max( *tgt_leaf );
            verify_min_max( *src_leaf );

            return base::bulk_append( *tgt_leaf, *src_leaf, inserted, end_pos, begin_leaf );
        }
        else // TODO in-the-middle partial bulk-inserts
        {
            auto const [inserted_count, consumed_source, tgt_next_leaf, tgt_next_offset]
            {
                merge
                (
                    *src_leaf, source_slot_offset,
                    *tgt_leaf, tgt_leaf_next_pos.pos,
                    unique
                )
            };
            tgt_leaf = tgt_next_leaf;
            if ( !tgt_leaf->is_root() ) {
                verify_min_max( *tgt_leaf );
            }
            tgt_leaf_next_pos.pos = tgt_next_offset;

            // merge might have caused a relocation (by calling split_to_insert)
            // TODO use iter_pos directly
            p_new_keys.update_pool_ptr( this->nodes_ );
            src_leaf = &leaf( source_slot );

            p_new_keys += consumed_source;
            inserted   += inserted_count;
        }

        if ( p_new_keys.pos() == end_pos ) [[ unlikely ]] {
            BOOST_ASSERT( source_slot == p_new_keys.pos().node );
            BOOST_ASSERT( !src_leaf->right );
            free( *src_leaf ); // see the comment below
            break;
        }

        if ( source_slot != p_new_keys.pos().node ) // have we moved to the next node?
        {
            // merged leaves (their contents) were effectively copied (or
            // potentially skipped, in case of unique trees) into existing
            // leaves (instead of simply linked into the tree structure) and now
            // have to be returned to the free pool
            // TODO: add leak detection to/for the entire bp_tree_impl class
            base::unlink_right( *src_leaf );
            free( *src_leaf );

            source_slot = p_new_keys.pos().node;
            src_leaf    = &leaf( source_slot );
        }
        source_slot_offset = p_new_keys.pos().value_offset;

        // seek the next position starting from the current one (relying on the
        // fact that we are using presorted data) rather than starting every
        // time from scratch (using find_insertion_point)
        std::tie( tgt_leaf, tgt_leaf_next_pos ) =
            find_next_insertion_point( *tgt_leaf, tgt_leaf_next_pos.pos, key_const_arg{ key_at( *src_leaf, source_slot_offset ) }, unique );
    }

    BOOST_ASSUME( inserted <= total_size );
    this->hdr().size_ += inserted;
    return inserted;
} // bp_tree_impl::insert()

template <typename Key, typename Comparator>
template <bool dedup_source>
bp_tree_impl<Key, Comparator>::size_type
bp_tree_impl<Key, Comparator>::insert_presorted_impl( std::span<Key const> const presorted_input, bool const unique )
{
    BOOST_ASSERT( std::ranges::is_sorted( presorted_input, comp() ) );

    if ( presorted_input.empty() )
        return 0;

    auto const total_size{ presorted_input.size() };

    // Helper lambda to create linked leaves from a contiguous span of presorted keys.
    // When dedup_source && unique, skips consecutive duplicates while copying.
    // Returns { first_leaf_slot, end_pos, actual_count_copied }.
    auto const do_dedup{ dedup_source && unique };
    auto const copy_to_nodes{ [this, do_dedup]( Key const * __restrict p_input, size_type count ) noexcept
        -> std::tuple<node_slot, iter_pos, size_type>
    {
        node_slot first_leaf_slot;
        node_slot prev_leaf_slot;
        size_type actual_copied{ 0 };
        auto const input_end{ p_input + count };

        while ( p_input != input_end )
        {
            if ( do_dedup )
            {
                // Skip leading dups against previous leaf's last key
                if ( actual_copied > 0 )
                {
                    auto const & prev_leaf{ this->leaf( prev_leaf_slot ) };
                    auto const & last_key{ key_at( prev_leaf, prev_leaf.num_vals - 1 ) };
                    while ( p_input != input_end && this->eq( *p_input, last_key ) )
                        ++p_input;
                    if ( p_input == input_end )
                        break;
                }
            }

            auto & new_leaf{ this->template new_node<leaf_node>() };
            auto const leaf_slot{ slot_of( new_leaf ) };
            node_size_type fill{ 0 };

            if ( do_dedup )
            {
                while ( p_input != input_end && fill < leaf_node::max_values )
                {
                    if ( fill > 0 && this->eq( *p_input, key_at( new_leaf, fill - 1 ) ) )
                    {
                        ++p_input;
                        continue;
                    }
                    key_at( new_leaf, fill++ ) = *p_input++;
                }
            }
            else
            {
                fill = static_cast<node_size_type>( std::min<size_type>(
                    static_cast<size_type>( input_end - p_input ),
                    leaf_node::max_values
                ));
                std::copy_n( p_input, fill, new_leaf.keys );
                p_input += fill;
            }

            new_leaf.num_vals  = fill;
            actual_copied     += fill;

            if ( !first_leaf_slot ) [[ unlikely ]] {
                first_leaf_slot = leaf_slot;
            } else {
                base::link( this->leaf( prev_leaf_slot ), new_leaf );
            }
            prev_leaf_slot = leaf_slot;
        }

        std::ignore = do_dedup; // avoid unused lambda capture warning when dedup_source is false
        return { first_leaf_slot, { prev_leaf_slot, this->leaf( prev_leaf_slot ).num_vals }, actual_copied };
    }}; // copy_to_nodes()

    this->reserve_additional( total_size * 4 / 3 ); // some headroom to account for splits and partial node fills

    if ( empty() ) [[ unlikely ]]
    {
        // Bulk insert into empty tree: create leaves directly from presorted input
        auto const [first_leaf_slot, end_pos, unique_count]{ copy_to_nodes( presorted_input.data(), total_size ) };
        base::bulk_insert_into_empty( first_leaf_slot, end_pos, unique_count );
        BOOST_ASSUME( this->hdr().size_ == unique_count );
        return unique_count;
    }

    // Insert into non-empty tree

    auto [tgt_leaf, tgt_leaf_next_pos]{ find_insertion_point( key_const_arg{ presorted_input[ 0 ] }, unique ) };

    size_type inserted{ 0 };
    size_type input_offset{ 0 };
    while ( input_offset < total_size )
    {
        // Skip duplicates for unique containers
        if ( tgt_leaf_next_pos.exact_find ) [[ unlikely ]]
        {
            BOOST_ASSUME( unique );
            ++input_offset;
        }
        else // Check for bulk append opportunity: at end of rightmost leaf
        if ( ( tgt_leaf_next_pos.pos == tgt_leaf->num_vals ) && !tgt_leaf->right )
        {
            // All remaining input goes after all existing data - use bulk append
            auto remaining_count{ total_size - input_offset };

            // First, fill up the current target leaf if there's space
            if ( auto const missing{ static_cast<node_size_type>( tgt_leaf->max_values - tgt_leaf->num_vals ) } )
            {
                if ( do_dedup )
                {
                    node_size_type fill{ 0 };
                    while ( input_offset < total_size && fill < missing )
                    {
                        bool const is_dup =
                            ( fill > 0 ) ? eq( presorted_input[ input_offset ], key_at( *tgt_leaf, tgt_leaf->num_vals + fill - 1 ) )
                                         : ( tgt_leaf->num_vals > 0 && eq( presorted_input[ input_offset ], key_at( *tgt_leaf, tgt_leaf->num_vals - 1 ) ) );
                        if ( !is_dup )
                            key_at( *tgt_leaf, tgt_leaf->num_vals + fill++ ) = presorted_input[ input_offset ];
                        ++input_offset;
                    }
                    tgt_leaf->num_vals += fill;
                    tgt_leaf->mark_dirty();
                    inserted        += fill;
                    remaining_count  = total_size - input_offset;
                }
                else
                {
                    auto const fill_size{ static_cast<node_size_type>( std::min<size_type>( remaining_count, missing ) ) };
                    std::copy_n( &presorted_input[ input_offset ], fill_size, &key_at( *tgt_leaf, tgt_leaf->num_vals ) );
                    tgt_leaf->num_vals += fill_size;
                    tgt_leaf->mark_dirty();
                    input_offset       += fill_size;
                    inserted           += fill_size;
                    remaining_count    -= fill_size;
                }

                if ( input_offset == total_size )
                    break; // to the trailing size_ update & return
            }

            // Skip any trailing dups of tgt_leaf's last key before creating
            // new leaves — copy_to_nodes has no context about tgt_leaf.
            if ( do_dedup )
            {
                auto const & last_key{ key_at( *tgt_leaf, tgt_leaf->num_vals - 1 ) };
                while ( input_offset < total_size && eq( presorted_input[ input_offset ], last_key ) )
                    ++input_offset;
                remaining_count = total_size - input_offset;
                if ( !remaining_count )
                    break;
            }

            // Create new linked leaves for remaining data
            auto const tgt_leaf_slot{ slot_of( *tgt_leaf ) };
            auto [first_new_leaf, end_pos, new_count]{ copy_to_nodes( &presorted_input[ input_offset ], remaining_count ) };
            tgt_leaf = &leaf( tgt_leaf_slot );

            // Link to existing tree
            auto & leftmost_new_leaf{ leaf( first_new_leaf ) };
            base::link( *tgt_leaf, leftmost_new_leaf );

            return base::bulk_append( *tgt_leaf, leftmost_new_leaf, inserted + new_count, end_pos, first_new_leaf );
        }
        else // Regular in-the-middle insertion (TODO partial bulk-inserts)
        {
            auto const remaining_count{ total_size - input_offset };
            auto const [inserted_count, consumed_source, tgt_next_leaf, tgt_next_offset]
            {
                merge
                (
                    &presorted_input[ input_offset ], static_cast<node_size_type>( std::min<size_type>( remaining_count, tgt_leaf->max_values ) ),
                    *tgt_leaf, tgt_leaf_next_pos.pos,
                    unique, do_dedup
                )
            };
            tgt_leaf = tgt_next_leaf;
            if ( !tgt_leaf->is_root() ) {
                verify_min_max( *tgt_leaf );
            }
            tgt_leaf_next_pos.pos = tgt_next_offset;

            input_offset += consumed_source;
            inserted     += inserted_count;
        }

        if ( input_offset == total_size )
            break;

        // For non-unique input into a unique tree: skip consecutive dups
        // before calling find_next_insertion_point. This avoids violating
        // find_from's precondition (when offset==num_vals it requires
        // key > back(), but a dup of the last inserted key is == back()).
        if constexpr ( dedup_source )
        {
            if ( unique )
            {
                while ( input_offset < total_size && input_offset > 0 && eq( presorted_input[ input_offset ], presorted_input[ input_offset - 1 ] ) )
                    ++input_offset;
                if ( input_offset == total_size )
                    break;
            }
        }

        // Find next insertion point, leveraging sorted input
        std::tie( tgt_leaf, tgt_leaf_next_pos ) =
            find_next_insertion_point( *tgt_leaf, tgt_leaf_next_pos.pos, key_const_arg{ presorted_input[ input_offset ] }, unique );
    }

    BOOST_ASSUME( inserted <= total_size );
    this->hdr().size_ += inserted;
    return inserted;
} // bp_tree_impl::insert_presorted_impl()

template <typename Key, typename Comparator>
bp_tree_impl<Key, Comparator>::size_type
bp_tree_impl<Key, Comparator>::merge( bp_tree_impl const & other, bool const unique )
{
    // Shares the same high-level structure as insert_presorted (empty-tree fast
    // path → find insertion point → merge/bulk_append loop → find_next), but the
    // input source differences (span vs cross-tree leaf chain with separate node
    // pool) are woven into every phase, making extraction of a common template
    // impractical without an input-source abstraction that would add complexity
    // without clear benefit.
    // Key differences from insert_presorted:
    //  - no need to copy and sort the input
    //  - bulk_append must first copy source nodes (not extractable from other tree)
    //  - node slot resolution goes through other's pool, not this->
    //  - no need to call update_pool_ptr since the source tree is not modified

    // (this is a refurbished move-merge implementation that only had its
    // explicitly other-destructive calls removed - good enough while only
    // trivial types are supported - TODO calls like move_entries will need to be
    // changed also to support non trivial types)

    if ( other.empty() )
        return 0;

    auto const total_size{ other.size() };

    if ( this->empty() ) [[ unlikely ]]
    {
        // Mirror the rvalue-merge's swap-when-empty optimization but for const
        // sources: copy other's leaf chain into freshly allocated nodes and
        // build the tree structure via bulk_insert_into_empty (same pattern as
        // insert_presorted's empty-tree fast path).
        bptree_base::reserve( other.used_number_of_nodes() );

        node_slot first_leaf_slot;
        node_slot prev_leaf_slot;

        for ( auto src_slot{ other.first_leaf() }; src_slot; )
        {
            auto const & src{ other.leaf( src_slot ) };
            auto & new_leaf{ this->template new_node<leaf_node>() };
            auto const leaf_slot{ slot_of( new_leaf ) };

            std::copy_n( src.keys, src.num_vals, new_leaf.keys );
            new_leaf.num_vals = src.num_vals;

            if ( !first_leaf_slot ) [[ unlikely ]] {
                first_leaf_slot = leaf_slot;
            } else {
                base::link( leaf( prev_leaf_slot ), new_leaf );
            }
            prev_leaf_slot = leaf_slot;
            src_slot = src.right;
        }

        base::bulk_insert_into_empty( first_leaf_slot, { prev_leaf_slot, leaf( prev_leaf_slot ).num_vals }, total_size );
        return total_size;
    }

    // We do not know the state (i.e. the fill factor) of the nodes from 'other'
    // and since we, for simplicity, copy them as-is in the tail-bulk phase we
    // cannot rely on the optimistic occupancy logic in reserve_additional to
    // produce the correct number (which would guarantee no further allocation
    // happens beyond this point).
    // ...this is still enough only in optimistic cases where all or most of the
    // merge happens at the (bulk) end - otherwise lots of splitting can occur
    // of intermediate leaves (with, then, lower load percentages)...
    //this->reserve_additional( total_size );
    bptree_base::reserve( this->used_number_of_nodes() + other.used_number_of_nodes() * 3 / 2 );

    auto const p_new_nodes_begin{ other.ra_begin() };
    auto const p_new_nodes_end  { other.ra_end  () };

    auto p_new_keys{ p_new_nodes_begin };
    auto [source_start_slot, source_slot_offset]{ p_new_keys.base().pos() };
    auto src_leaf{ &other.leaf( source_start_slot ) };

    auto [tgt_leaf, tgt_leaf_next_pos]{ find_insertion_point( *p_new_keys, unique ) };

    size_type inserted{ 0 };
    for ( ;; )
    {
        if ( tgt_leaf_next_pos.exact_find ) [[ unlikely ]]
        {
            BOOST_ASSUME( unique );
            ++p_new_keys;
            if ( p_new_keys == p_new_nodes_end ) [[ unlikely ]]
                break;
            continue;
        }

        BOOST_ASSUME( source_slot_offset < src_leaf->num_vals );
        // simple bulk_append at the end of the rightmost leaf
        if ( ( tgt_leaf_next_pos.pos == tgt_leaf->num_vals ) && !tgt_leaf->right )
        {
            // first fill up tgt_leaf (and handle when that's all that's left of
            // the input)
            if ( auto const remaining_tgt_node_space{ node_size_type( tgt_leaf->max_values - tgt_leaf->num_vals ) } )
            {
                node_size_type const remaining_src_node_data( src_leaf->num_vals - source_slot_offset );
                node_size_type const copy_size{ std::min( remaining_tgt_node_space, remaining_src_node_data ) };
                BOOST_ASSUME( copy_size );
                this->move_entries( *src_leaf, source_slot_offset, source_slot_offset + copy_size, *tgt_leaf, tgt_leaf->num_vals );
                tgt_leaf->num_vals += copy_size;
                tgt_leaf->mark_dirty();
                if ( copy_size == remaining_src_node_data )
                {
                    if ( !src_leaf->right )
                    {
                        // this breaks out fully/out of the main loop, skipping
                        // the bulk_append call below (as there is nothing more
                        // to append) and the update of 'inserted' right after
                        // it so we have to update it here
                        inserted += copy_size;
                        break;
                    }
                    src_leaf = &other.right( *src_leaf );
                    source_slot_offset = 0;
                }
                else
                {
                    source_slot_offset += copy_size;
                }
            }

            // pre-copy the (remainder of the) source into fresh nodes in
            // order to simply call bulk_append
            node_slot src_copy_begin;
            node_slot prev_src_copy_node;
            for ( ;; )
            {
                BOOST_ASSUME( src_leaf->num_vals );
                //...mrmlj...see the note for reserve
                //BOOST_ASSUME( this->hdr().free_node_count_ ); // verify that we have preallocated/reserved enough nodes so that we can assume tgt_leaf references to be stable (src_leaf comes from 'other')
                auto const tgt_slot{ slot_of( *tgt_leaf ) };
                auto & src_leaf_copy{ this->template new_node<leaf_node>() };
                tgt_leaf = &leaf( tgt_slot );
                if ( !src_copy_begin ) [[ unlikely ]]
                {
                    src_copy_begin = slot_of( src_leaf_copy );
                    this->move_entries( *src_leaf, source_slot_offset, src_leaf->num_vals, src_leaf_copy, 0 );
                    src_leaf_copy.num_vals = src_leaf->num_vals - source_slot_offset;
#               if 0 // actually there should be no need for this src update (see the note at the end of the function for a TODO on proper src/other cleanup)
                    src_leaf->num_vals     = source_slot_offset;
#               endif
                    this->link( *tgt_leaf, src_leaf_copy );
                    prev_src_copy_node = tgt_slot; //...mrmlj...so that it can be used to fetch last_src_copy_node below
                    // TODO examine if we need the logic involving
                    // append_and_free here as well (as in insert())
                    this->bulk_append_fill_leaf_if_incomplete( src_leaf_copy );
                }
                else
                {
                    this->move_entries( *src_leaf, 0, src_leaf->num_vals, src_leaf_copy, 0 );
                    src_leaf_copy.num_vals = src_leaf->num_vals;
#               if 0 // see above
                    src_leaf->num_vals     = 0;
#               endif
                    this->link( this->leaf( prev_src_copy_node ), src_leaf_copy );
                }
                BOOST_ASSUME( !src_leaf_copy.parent );
                BOOST_ASSUME( !src_leaf_copy.tail.parent_child_idx );
                    
                if ( !src_leaf->right )
                    break;
                src_leaf           = &other.right( *src_leaf );
                prev_src_copy_node = slot_of( src_leaf_copy );
            }

            auto const so_far_consumed{ static_cast<size_type>( p_new_keys - p_new_nodes_begin ) };
            BOOST_ASSUME( so_far_consumed < total_size );
            inserted += static_cast<size_type>( total_size - so_far_consumed );
            auto const last_src_copy_node{ this->leaf( prev_src_copy_node ).right };
            iter_pos const end_pos{ last_src_copy_node, src_leaf->num_vals };
            return base::bulk_append( *tgt_leaf, this->leaf( src_copy_begin ), inserted, end_pos, src_copy_begin );
        }
        // TODO in-the-middle partial bulk-inserts

        auto const [inserted_count, consumed_source, tgt_next_leaf, tgt_next_offset]
        {
            merge
            (
                *src_leaf, source_slot_offset,
                *tgt_leaf, tgt_leaf_next_pos.pos,
                unique
            )
        };
        tgt_leaf = tgt_next_leaf;
        if ( !tgt_leaf->is_root() )
            verify_min_max( *tgt_leaf );

        p_new_keys += consumed_source;
        inserted   += inserted_count;

        if ( p_new_keys == p_new_nodes_end ) [[ unlikely ]]
            break;

        auto const new_pos{ p_new_keys.base().pos() };
        src_leaf           = &other.leaf( new_pos.node );
        source_slot_offset = new_pos.value_offset;
        BOOST_ASSUME( src_leaf->num_vals );

        std::tie( tgt_leaf, tgt_leaf_next_pos ) =
            find_next_insertion_point( *tgt_leaf, tgt_next_offset, key_const_arg{ key_at( *src_leaf, source_slot_offset ) }, unique );
    }

    BOOST_ASSUME( inserted <= total_size );
    this->hdr().size_ += inserted;

    return inserted;
} // bp_tree_impl::merge()

template <typename Key, typename Comparator>
bp_tree_impl<Key, Comparator>::size_type
bp_tree_impl<Key, Comparator>::merge( bp_tree_impl && other, bool const unique )
{
    if ( this->empty() ) {
        swap( other );
        return size();
    }

    // does not actually move-out values - makes no difference currently (with
    // only trivial types support) - TODO
    auto const inserted{ merge( std::as_const( other ), unique ) };
    // TODO significant modifications will be required here when adding support
    // for non trivial types: avoid redundant destruction of moved out values -
    // make sure all are moved out or in-situ reset/destroyed (in case of
    // duplicates and unique trees) so that merely the storage of other can be
    // freed here.
    static_assert( std::is_trivially_destructible_v<Key> );
    other.reset();
    return inserted;
}


PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
