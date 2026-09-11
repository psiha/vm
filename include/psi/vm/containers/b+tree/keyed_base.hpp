#pragma once
////////////////////////////////////////////////////////////////////////////////
///
/// Everything that knows the key type but not how keys are ordered: the leaf
/// and inner node layouts, moving entries between nodes, splitting, merging,
/// underflow, the bulk-fill paths, and the key-typed iterators.
///
////////////////////////////////////////////////////////////////////////////////

#include "base.hpp"

//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

PSI_WARNING_DISABLE_PUSH()
PSI_WARNING_MSVC_DISABLE( 4127 ) // conditional expression is constant
PSI_WARNING_MSVC_DISABLE( 5030 ) // unrecognized attribute



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
    static_assert( std::is_trivially_copyable_v<Key> );

    bptree_base_wkey(                                 ) noexcept = default;
    bptree_base_wkey( bptree_base_wkey const & source ) : bptree_base{ source } {}
    bptree_base_wkey( bptree_base_wkey &&             ) noexcept = default;
    bptree_base_wkey & operator=( bptree_base_wkey && ) noexcept = default;

    using key_rv_arg    = std::conditional_t<can_be_passed_in_reg<Key>, Key const, pass_rv_in_reg<Key>>;
    using key_const_arg = std::conditional_t<can_be_passed_in_reg<Key>, Key const, pass_in_reg   <Key>>;

    class [[ clang::trivial_abi, gsl::Pointer ]] fwd_iterator;
    class [[ clang::trivial_abi, gsl::Pointer ]]  ra_iterator;
    class [[ clang::trivial_abi, gsl::Pointer ]] leaf_iterator;

    using       iterator = fwd_iterator;
    using const_iterator = std::basic_const_iterator<iterator>;

public:
    static constexpr size_type max_size() noexcept
    {
        auto const max_number_of_nodes     { std::numeric_limits<typename node_slot::value_type>::max() };
        auto const max_number_of_leaf_nodes{ /*TODO*/ max_number_of_nodes };
        auto const values_per_node         { leaf_node::max_values };
        auto const max_sz                  { max_number_of_leaf_nodes * values_per_node };
        static_assert( max_sz <= std::numeric_limits<size_type>::max() );
        return max_sz;
    }

    storage_result map_memory( size_type initial_capacity = 0 ) noexcept { return bptree_base::map_memory( node_count_required_for_values( initial_capacity ) ); }
    size_type capacity() const noexcept
    {
        auto const n{ nodes_.capacity() };
        if ( !n ) [[ unlikely ]]
            return 0;

        // Iteratively compute how many of n node slots can be leaves:
        // start by assuming all are leaves, compute the inner node overhead
        // for that many leaves (maximally branching), subtract, converge.
        auto leaf_count{ n };
        for ( ;; )
        {
            node_slot::value_type inner_count{ 0 };
            auto level{ leaf_count };
            while ( level > 1 )
            {
                level = divide_up( level, inner_node::max_children );
                inner_count += level;
            }
            auto const new_leaf_count{ n - inner_count };
            if ( new_leaf_count == leaf_count )
                break;
            leaf_count = new_leaf_count;
        }
        return leaf_count * leaf_node::max_values;
    }

    void reserve_additional( size_type const additional_values ) { bptree_base::reserve_additional( node_count_required_for_values( additional_values ) ); }
    void reserve           ( size_type const new_capacity      ) { bptree_base::reserve           ( node_count_required_for_values( new_capacity      ) ); }

    const_iterator erase( const_iterator iter ) noexcept;
    const_iterator erase( const_iterator first, const_iterator last ) noexcept;

    // optimized version of std::copy( bpt.begin(), bpt.end(), vec.begin() )
    template <typename Proj = std::identity>
    auto flatten(                                           std::output_iterator<std::invoke_result_t<Proj &, Key const &>> auto output, size_type available_space, Proj proj = {} ) const noexcept( std::is_nothrow_invocable_v<Proj &, Key const &> );
    template <typename Proj = std::identity>
    auto flatten( const_iterator begin, const_iterator end, std::output_iterator<std::invoke_result_t<Proj &, Key const &>> auto output, size_type available_space, Proj proj = {} ) const noexcept( std::is_nothrow_invocable_v<Proj &, Key const &> );

    // Leaf-node iteration: walk the doubly-linked list of leaves directly,
    // dereferencing to std::span<Key const> of each leaf's keys.  Lets callers
    // express a two-level loop ('for each leaf { for each key }') that avoids
    // the per-step bookkeeping of fwd_iterator.
    [[ gnu::pure ]] leaf_iterator node_begin() const noexcept;
    [[ gnu::pure ]] leaf_iterator node_end  () const noexcept;
    // Range facade: `for ( auto span : tree.leaves() ) { for ( auto k : span ) ... }`.
    [[ gnu::pure ]] auto leaves() const noexcept { return std::ranges::subrange{ node_begin(), node_end() }; }

    // solely a debugging helper (include b+tree_print.hpp)
    void print() const;

protected: // node types
    struct alignas( node_size ) parent_node : node_header
    {
        static auto constexpr storage_space{ node_size - align_up( sizeof( node_header ), alignof( Key ) ) };

        // https://stackoverflow.com/questions/59362113/b-tree-minimum-internal-children-count-explanation
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

        static node_size_type constexpr max_children{ order }; // 'cardinality'
        static node_size_type constexpr max_values  { max_children - 1 };

        Key       keys    [ max_values   ];
        node_slot children[ max_children ];
    }; // struct parent_node

    struct inner_node : parent_node
    {
        static node_size_type constexpr min_children{ ihalf_ceil<parent_node::max_children> };
        static node_size_type constexpr min_values  { min_children - 1 };

        // Allowing for min two children would theoretically be possible but it
        // would complicate underflow handling (as it would lead to a bogus
        // intermediate state where the two children would get merged) and isn't
        // worth supporting (bordering on a plain BST).
        static_assert( min_children >= 3 );
        // see the leaf_node twin: the 2-into-1 merge has to fit
        static_assert( 2 * min_children <= parent_node::max_children + 1 );
    }; // struct inner_node

    struct root_node : parent_node
    {
        static node_size_type constexpr min_children{ 2 };
        static node_size_type constexpr min_values  { min_children - 1 };
    }; // struct root_node

    struct alignas( node_size ) leaf_node : node_header
    {
        // TODO support for maps (i.e. keys+values)
        using value_type = Key;

        static node_size_type constexpr storage_space{ static_cast<node_size_type>( node_size - align_up( sizeof( node_header ), alignof( Key ) ) ) };
        static node_size_type constexpr max_values   { storage_space / sizeof( Key ) };
        static node_size_type constexpr min_values   { ihalf_ceil<max_values> };

        // The tree's central inequality, and the reason a minimum fill of half
        // the capacity is not an arbitrary choice: it is exactly what makes
        // "either a sibling can lend a value, or the two of them merge into
        // one node" true, which is what the entire underflow half of the tree
        // (handle_underflow, merge_right_into_left, append_and_free and the
        // bulk-fill partitions written as `min_values * 2`) rests on.  Raising
        // the minimum past it does not merely make those sites suboptimal - it
        // makes the 2-into-1 merge overflow the node, silently, before it
        // asserts.  A higher fill target therefore has to bring its own merge
        // shape (3-into-2) and re-state this bound; it can never be a constant
        // bump.
        static_assert( 2 * min_values <= max_values + 1 );

        Key keys[ max_values ];
    }; // struct leaf_node

    static_assert( sizeof( inner_node ) == node_size );
    static_assert( sizeof(  leaf_node ) == node_size );

protected: // split_to_insert and its helpers
    root_node & new_root( node_slot const left_child, node_slot const right_child, key_rv_arg separator_key )
    {
        auto & new_root_node{ as<root_node>( bptree_base::new_root( left_child, right_child ) ) };
        key_at( new_root_node, 0 ) = std::move( separator_key );
        new_root_node.children[ 0 ] =  left_child;
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

        // the old node gets the minimum/median/mid -> the new gets the leftovers (i.e. mid or mid + 1)
        new_node.num_vals = max - mid;

        value_type key_to_propagate;
        if ( new_insert_pos == 0 )
        {
            key_to_propagate = std::move( value );

            move_entries  ( node, mid    , node.num_vals    , new_node, 0 );
            move_chldrn( node, mid + 1, node.num_vals + 1, new_node, 1 );
        }
        else
        {
            key_to_propagate = std::move( key_at( node, mid ) );

            move_entries  ( node, mid + 1, insert_pos    , new_node, 0 );
            move_chldrn( node, mid + 1, insert_pos + 1, new_node, 0 );

            move_entries  ( node, insert_pos    , node.num_vals    , new_node, new_insert_pos     );
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

        move_entries( node, mid       , insert_pos, new_node, 0                  );
        move_entries( node, insert_pos, max       , new_node, new_insert_pos + 1 );

        node    .num_vals = mid          ;
        new_node.num_vals = max - mid + 1;

        keys( new_node )[ new_insert_pos ] = std::move( value );
        auto const & key_to_propagate{ key_at( new_node, 0 ) };

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

        value_type key_to_propagate{ std::move( key_at( node, mid - 1 ) ) };

        move_entries  ( node, mid, num_vals  ( node ), new_node, 0 );
        move_chldrn( node, mid, num_chldrn( node ), new_node, 0 );

        rshift_entries  ( node, insert_pos    , mid     );
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

          move_entries( node, mid - 1   , node.num_vals, new_node, 0 );
        rshift_entries( node, insert_pos, mid                       );

        node    .num_vals = mid;
        new_node.num_vals = max - mid + 1;

        keys( node )[ insert_pos ] = std::move( value );
        auto const & key_to_propagate{ key_at( new_node, 0 ) };

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
        auto [split_slot, new_slot]{ bptree_base::new_spillover_node_for( node_to_split ) };
        auto p_node    { &node<N>( split_slot ) };
        auto p_new_node{ &node<N>(  new_slot ) };
        BOOST_ASSUME( p_node->num_vals == max );
        BOOST_ASSERT
        (
            !p_node->parent || ( inner( p_node->parent ).children[ p_node->parent_child_idx ] == split_slot )
        );

        auto const new_insert_pos         { insert_pos - mid };
        bool const insertion_into_new_node{ new_insert_pos >= 0 };
        auto [key_to_propagate, next_insert_pos]{ insertion_into_new_node // we cannot save a reference here because it might get invalidated by the new_node<root_node>() call below
            ? insert_into_new_node     ( *p_node, *p_new_node, std::move( value ), insert_pos, static_cast<node_size_type>( new_insert_pos ), key_right_child )
            : insert_into_existing_node( *p_node, *p_new_node, std::move( value ), insert_pos,                                                key_right_child )
        };

        verify_min_max( *p_node     );
        verify_min_max( *p_new_node );
        BOOST_ASSUME( p_node->num_vals == mid );

        if ( std::is_same_v<N, leaf_node> && !p_new_node->right ) {
            set_last_leaf( hdr(), new_slot );
        }

        // propagate the mid key to the parent
        if ( p_node->is_root() ) [[ unlikely ]] {
            new_root( split_slot, new_slot, std::move( key_to_propagate ) );
        } else {
            auto const key_pos{ static_cast<node_size_type>( p_new_node->parent_child_idx /*it is the _right_ child*/ - 1 ) };
            insert( parent( *p_node ), key_pos, std::move( key_to_propagate ), new_slot );
        }
        return insertion_into_new_node
            ? insert_pos_t{  new_slot, next_insert_pos }
            : insert_pos_t{ split_slot, next_insert_pos };
    }

    // Dual of handle_underflow's borrow branches, in the other direction: an
    // overflowing node hands values over to a sibling that still has room
    // instead of splitting, and the separator between the two follows the
    // values exactly as it does when the flow is the other way.  This is what
    // buys a fill above the ~ln2 a plain split-on-full settles at, without
    // moving the minimum-fill floor (which cannot be raised while merges stay
    // 2-into-1 - see the bound asserted at leaf_node::min_values).
    //
    // Leaves only, deliberately.  A node's children carry a back-index into
    // their parent (node_header::parent_child_idx), so relocating an
    // inner node's children re-indexes - and dirties - every one of them,
    // costing more than the split it would save; leaves are the overwhelming
    // majority of nodes at any realistic fanout, so that is where the fill is.
    //
    // Returns where the pending insertion ended up, or nothing when no sibling
    // could take the load - in which case the caller splits as before.
    [[ nodiscard ]] std::optional<iter_pos>
    relieve_into_sibling( leaf_node & node, node_size_type const insert_pos ) noexcept
    {
        auto constexpr max{ leaf_node::max_values };
        BOOST_ASSUME( node.num_vals == max );
        BOOST_ASSUME( insert_pos    <= max );
        if ( node.is_root() ) [[ unlikely ]] // no siblings to relieve into
            return {};

        auto const   this_slot       { slot_of( node ) };
        auto       & parent          { this->parent( node ) };
        auto const   parent_child_idx{ node.parent_child_idx };
        BOOST_ASSUME( parent.children[ parent_child_idx ] == this_slot );

        // the left/right links are level links which can point across parents,
        // so sibling existence is resolved from the parent's child count - the
        // same idiom handle_underflow uses - and only then dereferenced
        auto const has_left { parent_child_idx > 0 };
        auto const has_right{ parent_child_idx < ( num_chldrn( parent ) - 1 ) };
        auto const p_left   { has_left  ? &left ( node ) : nullptr };
        auto const p_right  { has_right ? &right( node ) : nullptr };
        // Room is the slots a sibling can actually receive into, and the two
        // sides do not count it the same way.  The LEFT sibling receives at its
        // TAIL - move_entries writes at start + num_vals - so a front gap does
        // not help it and has to come off the total.  The RIGHT sibling receives
        // at its FRONT, where its gap is precisely what supplies the slots (the
        // branch below spends it first and shifts only the deficit).
        auto const left_room ( p_left  ? max - p_left ->start - p_left ->num_vals : 0 );
        auto const right_room( p_right ? max                  - p_right->num_vals : 0 );
        // Half of a single free slot is nothing, and handing over that one slot
        // would leave the sibling full - so it has to be worth a move.
        if ( std::max( left_room, right_room ) < 2 )
            return {};

        // Hand over half of the room found, rounded down, not all of it: the
        // sibling is a node in its own right, emptying its slack here only
        // moves the next split one node over, and a sibling left full would
        // have to be split by the very insertion this is trying to relieve.
        if ( left_room >= right_room )
        {
            auto & left_sibling{ *p_left };
            auto const to_move       { static_cast<node_size_type>( left_room / 2 ) };
            auto const left_prior_num{ left_sibling.num_vals };
            move_entries( node, 0, to_move, left_sibling, left_prior_num );
            // the node was full, so it had no gap; the entries it keeps simply
            // begin further in - no move at all
            BOOST_ASSUME( node.start == 0 );
            if ( to_move <= leaf_node::max_front_gap ) node.start = static_cast<node_size_type>( to_move );
            else                                       shift_entries_left( node, 0, max, to_move );
            left_sibling.num_vals = static_cast<node_size_type>( left_prior_num + to_move );
            node        .num_vals = static_cast<node_size_type>( max            - to_move );
            left_sibling.mark_dirty();
            node        .mark_dirty();
            // this node's first key moved, so its separator has to follow
            update_separator( node, key_at( node, 0 ) );
            verify_min_max( left_sibling );
            verify_min_max( node         );
            return ( insert_pos < to_move )
                ? iter_pos{ slot_of( left_sibling ), static_cast<node_size_type>( left_prior_num + insert_pos ) }
                : iter_pos{ this_slot              , static_cast<node_size_type>( insert_pos     - to_move    ) };
        }
        else
        {
            auto & right_sibling{ *p_right };
            auto const to_move   { static_cast<node_size_type>( right_room / 2 ) };
            auto const kept      { static_cast<node_size_type>( max - to_move ) };
            // Opening 'to_move' slots at the sibling's front: its existing gap
            // supplies some of them, so only the DEFICIT has to be shifted -
            // shifting by the full amount from a base that is already offset
            // runs past the end of the array (the room check bounds
            // num_vals + to_move, not start + num_vals + to_move).
            if ( right_sibling.start >= to_move ) {
                right_sibling.start -= to_move;
            } else {
                auto const deficit{ static_cast<node_size_type>( to_move - right_sibling.start ) };
                shift_entries_right( right_sibling, 0, right_sibling.num_vals + deficit, deficit );
                right_sibling.start = 0;
            }
            move_entries( node, kept, max, right_sibling, 0 );
            right_sibling.num_vals = static_cast<node_size_type>( right_sibling.num_vals + to_move );
            node         .num_vals = kept;
            right_sibling.mark_dirty();
            node         .mark_dirty();
            // the sibling's first key moved, so its separator has to follow
            update_separator( right_sibling, key_at( right_sibling, 0 ) );
            verify_min_max( right_sibling );
            verify_min_max( node          );
            return ( insert_pos > kept )
                ? iter_pos{ slot_of( right_sibling ), static_cast<node_size_type>( insert_pos - kept ) }
                : iter_pos{ this_slot               , insert_pos                                        };
        }
    }

    // The overflow decision itself: relieve into a sibling when the policy asks
    // for it and one has room, split otherwise.
    template <typename N>
    insert_pos_t overflow_to_insert( N & node, node_size_type const insert_pos, key_rv_arg value, node_slot const key_right_child )
    {
        if constexpr ( redistribute_on_overflow && std::is_same_v<N, leaf_node> )
        {
            if ( auto const relieved{ relieve_into_sibling( node, insert_pos ) } )
            {
                auto & target{ leaf( relieved->node ) };
                // an assert, not an assume: full() runs verify(), and an
                // assumption carrying a side effect is discarded (with a
                // diagnostic) rather than believed
                BOOST_ASSERT( !full( target ) );
                return insert( target, relieved->value_offset, std::move( value ), key_right_child );
            }
        }
        return split_to_insert( node, insert_pos, std::move( value ), key_right_child );
    }


protected: // 'other'
    // key_locations (containing find_pos) should also be returnable through registers
    using find_pos = std::conditional_t
    <
        ( sizeof( find_pos1 ) > 2 ) &&
        ( leaf_node::max_values <= ( std::numeric_limits<node_size_type>::max() / 2 ) ), // we get only half the range if one bit is shaved off for exact_find
        bptree_base::find_pos0,
        bptree_base::find_pos1
    >;
    struct key_locations
    {
        leaf_node & leaf;
        find_pos    leaf_offset;
        // optional - if also present in an inner node as a separator key
        node_size_type inner_offset; // ordered for compact layout
        node_slot      inner;
    }; // struct key_locations

    // internal deque-like simpler/faster random access iterator for/over full
    // nodes (used for sorting input data in bulk insert operations)
    class [[ clang::trivial_abi ]] ra_full_node_iterator;

    [[ gnu::pure, nodiscard ]] const_iterator make_iter( auto const &... args    ) const noexcept { return static_cast<iterator &&>( const_cast<bptree_base_wkey &>( *this ).bptree_base::make_iter( args... ) ); }
    [[ gnu::pure, nodiscard ]] const_iterator make_iter( key_locations const loc ) const noexcept { return make_iter( loc.leaf, static_cast<node_size_type>( loc.leaf_offset.pos ) ); }

    template <typename N>
    insert_pos_t insert( N & target_node, node_size_type const target_node_pos, key_rv_arg v, node_slot const right_child )
    {
        verify( target_node );
        if ( full( target_node ) ) [[ unlikely ]] {
            return overflow_to_insert( target_node, target_node_pos, std::move( v ), right_child );
        } else {
            ++target_node.num_vals;
            if constexpr ( requires { target_node.children; } ) {
                rshift_entries( target_node, target_node_pos );
            } else {
                // a leaf opens the slot from whichever side is cheaper, and
                // MUST use the front when its entries already reach the end of
                // their array
                bool const room_behind{ target_node.start + target_node.num_vals <= N::max_values };
                BOOST_ASSUME( room_behind || target_node.start );
                if ( target_node.start && ( !room_behind || ( target_node_pos * 2u < target_node.num_vals ) ) )
                    open_slot_from_front( target_node, target_node_pos );
                else
                    rshift_entries( target_node, target_node_pos );
            }
            key_at( target_node, target_node_pos ) = std::move( v );
            target_node.mark_dirty();
            if constexpr ( requires { target_node.children; } ) {
                node_size_type const ch_pos( target_node_pos + /*>right< child*/ 1 );
                rshift_chldrn( target_node, ch_pos );
                this->insrt_child( target_node, ch_pos, right_child );
            } else {
                // prior to Dec 11th 2025 this check was not here yet everything
                // worked (as if always going into the !leaf.left early exit) -
                // adding this check 'defensively' - TODO: investigate
                if ( !target_node_pos ) [[ unlikely ]] {
                    update_separator( target_node, v );
                }
            }
            return { slot_of( target_node ), static_cast<node_size_type>( target_node_pos + 1 ) };
        }
    }
    [[ gnu::sysv_abi, gnu::noinline ]]
    iter_pos erase( leaf_node & leaf, node_size_type const leaf_key_offset ) noexcept
    {
        lshift_entries( leaf, leaf_key_offset );
        --leaf.num_vals;
        leaf.mark_dirty();

        iter_pos next_pos{ slot_of( leaf ), leaf_key_offset };

        auto & hdr   { this->hdr() };
        auto & depth_{ hdr.depth_ };
        if ( depth_ == 1 ) [[ unlikely ]] // handle 'leaf root' deletion directly to simplify handle_underflow()
        {
            auto & root_{ hdr.root_ };
            BOOST_ASSUME( root_ == slot_of( leaf ) );
            BOOST_ASSUME( leaf.is_root() );
            BOOST_ASSUME( hdr.size_ == static_cast<size_t>( leaf.num_vals ) + 1 );
            BOOST_ASSUME( !leaf.left  );
            BOOST_ASSUME( !leaf.right );
            if ( leaf.num_vals == 0 )
            {
                BOOST_ASSUME( hdr.first_leaf_ == root_ );
                BOOST_ASSUME( hdr.last_leaf_  == root_ );
                root_ = hdr.first_leaf_ = hdr.last_leaf_ = {};
                bptree_base::free( leaf );
                --depth_;
                BOOST_ASSUME( depth_ == 0 );
                BOOST_ASSUME( hdr.size_ == 1 );
                next_pos = {}; // empty end_pos
            }
        }
        else
        {
            bool last_node_value_was_erased{ leaf_key_offset == leaf.num_vals };
            auto p_leaf{ &leaf };
            if ( underflowed( leaf ) )
            {
                BOOST_ASSUME( !leaf.is_root() );
                BOOST_ASSUME( depth_ > 1 );
                next_pos               = handle_underflow( leaf );
                next_pos.value_offset += leaf_key_offset;
                p_leaf                 = &this->leaf( next_pos.node );
                BOOST_ASSUME( next_pos.value_offset <= p_leaf->num_vals );
                last_node_value_was_erased = ( next_pos.value_offset == p_leaf->num_vals );
            }

            if ( last_node_value_was_erased )
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

    [[ gnu::sysv_abi, gnu::noinline ]]
    bool erase_single( key_locations const location ) noexcept
    {
        BOOST_ASSUME( location.leaf_offset.exact_find );
        leaf_node & leaf{ location.leaf };
        auto const  leaf_key_offset{ location.leaf_offset.pos };
        if ( location.inner ) [[ unlikely ]] // "most keys are (only) in the leaf nodes"
        {
            BOOST_ASSUME( leaf_key_offset == 0 );

            auto & inner        { this->inner( location.inner ) };
            auto & separator_key{ key_at( inner, location.inner_offset ) };
            BOOST_ASSUME( leaf_key_offset + 1 < leaf.num_vals );
            static_assert( leaf_node::min_values > 1 ); // makes this simpler to handle: we can assume that key_at( leaf, 1 ) exists
            separator_key = key_at( leaf, leaf_key_offset + 1 );
            inner.mark_dirty();
        }

        erase( leaf, leaf_key_offset );
        return true; // courtesy return to enable tail calls
    }

    // underflow handler helper for nonunique or bulk/range erase
    iter_pos check_and_handle_bulk_erase_underflow( leaf_node & node ) noexcept
    {
        iter_pos pos{ slot_of( node ), 0 };
        if ( node.is_root() ) [[ unlikely ]]
        {
            BOOST_ASSERT( !underflowed( as<root_node>( node ) ) ); // otherwise it should have been erased completely (underflowed root == empty root)
            return pos;
        }
        // handle_underflow is designed for unique data, as such it may fill in
        // only a single missing value - for now call it in a loop (until a
        // specialized nonunique version becomes necessary)
        auto p_node( &node );
        while ( underflowed( *p_node ) )
        {
            pos = handle_underflow( *p_node );
            p_node = &this->leaf( pos.node );
        }
        return pos;
    }

    void remove_from_parent( inner_node & __restrict parent, node_size_type const child_idx ) noexcept
    {
        verify( parent );
        // for the leftmost child we also/simply delete the lead key (and the
        // logic just works out)
        auto const key_idx{ static_cast<node_size_type>( std::max( 0, child_idx - 1 ) ) };
        lshift_entries  ( parent,   key_idx );
        lshift_chldrn( parent, child_idx );
        parent.num_vals--;
        parent.mark_dirty();
        BOOST_ASSUME( parent.num_vals || parent.is_root() );

        // propagate underflow
        auto & depth_{ this->hdr().depth_ };
        auto & root_ { this->hdr().root_  };
        if ( parent.is_root() ) [[ unlikely ]]
        {
            BOOST_ASSUME( root_ == slot_of( parent ) );
            auto & root{ as<root_node>( parent ) };
            BOOST_ASSUME( !!root.children[ 0 ] );
            if ( underflowed( root ) )
            {
                // the last, lone child becomes the new root
                root_ = root.children[ 0 ];
                auto & new_root_node{ bptree_base::node<root_node>( root_ ) };
                new_root_node.parent = {};
                new_root_node.mark_dirty();
                --depth_;
                free( root );
            }
        }
        else
        if ( underflowed( parent ) )
        {
            handle_underflow( parent );
        }
    }
    void remove_from_parent( node_header const & node ) noexcept
    {
        remove_from_parent( inner( node.parent ), node.parent_child_idx );
    }


    struct bulk_copied_input
    {
        node_slot begin;
        iter_pos  end;
        size_type size;
        // save a linearized array of (full) nodes in order to be able to use
        // "really random access iterators" (similar to std::deque iterators)
        // for subsequent sorting
        heap_vector<leaf_node *, std::uint32_t> nodes;
    }; // struct bulk_copied_input

    template <typename I, typename S, std::ranges::subrange_kind kind>
    bulk_copied_input
    bulk_insert_prepare( std::ranges::subrange<I, S, kind> keys )
    {
        auto constexpr can_preallocate{ kind == std::ranges::subrange_kind::sized };
        size_type input_size;
        heap_vector<leaf_node *, std::uint32_t> nodes;
        auto p_node{ nodes.end() };
        if constexpr ( can_preallocate ) {
            input_size = static_cast<size_type>( keys.size() );
            if ( !input_size ) [[ unlikely ]] // minor optimization for 'complex' ranges (like complex/compound views which have size methods but which are non trivial) - reuse size info for empty check
                return bulk_copied_input{};
            auto const required_nodes{ node_count_required_for_values( input_size ) };
            nodes.grow_to( required_nodes, default_init );
            p_node = nodes.begin();
            bptree_base::reserve_additional( required_nodes );
        } else {
            if ( keys.empty() ) [[ unlikely ]]
                return bulk_copied_input{};
            input_size = 0;
            //bptree_base::reserve_additional( 42 ); // ? assume big(ger) data
        }
        // w/o preallocation a saved hdr reference could get invalidated
        auto const begin    { can_preallocate ? hdr().free_list_ : slot_of( new_node<leaf_node>() ) };
        auto       leaf_slot{ begin };
        auto       p_keys{ keys.begin() };
        size_type  count{ 0 };
        for ( ;; )
        {
            auto & leaf{ this->leaf( leaf_slot ) };
            BOOST_ASSUME( leaf.num_vals == 0 );
            // fill this leaf
            if constexpr ( can_preallocate ) {
                auto const size_to_copy{ static_cast<node_size_type>( std::min<size_type>( leaf.max_values, input_size - count ) ) };
                BOOST_ASSUME( size_to_copy > 0 );
                std::copy_n ( p_keys, size_to_copy, &key_at( leaf, 0 ) );
                std::advance( p_keys, size_to_copy );
                leaf.num_vals  = size_to_copy;
                // The leaf is being pulled directly off the free list (not via
                // new_node()), so its persisted dirty bit is whatever the prior
                // commit_to left behind — typically clean. Without re-marking,
                // the next commit_to would skip this freshly-populated leaf and
                // leave stale bytes in master, blowing up later tree walks.
                leaf.mark_dirty();
                count         += size_to_copy;
                *p_node++      = &leaf;
                BOOST_ASSUME( hdr().free_node_count_ ); // manual/local free node accounting
                --this->hdr().free_node_count_;
            } else {
                BOOST_ASSUME( !input_size );
                while ( ( p_keys != keys.end() ) && ( leaf.num_vals < leaf.max_values ) ) {
                    key_at( leaf, leaf.num_vals++ ) = *p_keys++;
                }
                count += leaf.num_vals;
                // ugh - cannot save pointers right away as they may get
                // invalidated by calls to new_node
                nodes.push_back( reinterpret_cast<leaf_node * const &>( leaf_slot ) );
            }

            BOOST_ASSUME( leaf.num_vals > 0 );

            // move to the next one or cleanup if we are at the end and return
            if constexpr ( can_preallocate ) {
                if ( count != input_size ) {
                    leaf_slot = leaf.right;
                    continue;
                } else {
                    this->hdr().free_list_ = leaf.right;
                    unlink_right( leaf );
                    BOOST_ASSERT( p_keys == keys.end() );
                    BOOST_ASSUME( count == input_size );
                    count = input_size; // help the compiler eliminate the accumulation code above
                }
            } else {
                BOOST_ASSUME( !input_size );
                if ( p_keys != keys.end() ) {
                    auto & new_leaf{ new_node<leaf_node>() };
                    link( this->leaf( leaf_slot ), new_leaf ); // new_node could have invalidated the 'leaf' reference so it must not be used anymore
                    leaf_slot = slot_of( new_leaf );
                    continue;
                }
#           ifdef __clang__
                #pragma clang loop unroll( disable )
#           endif
                for ( auto & leaf_ptr : nodes ) {
                    leaf_ptr = &this->leaf( reinterpret_cast<node_slot const &>( leaf_ptr ) );
                }
            }
            return bulk_copied_input{ begin, { leaf_slot, leaf.num_vals }, count, std::move( nodes ) };
        }
        std::unreachable();
    }
    // Deduplicating a bulk_copied_input (for a unique tree) leaves its trailing
    // leaves unused: shrink the last still-used one to its new fill, cut the
    // chain there and hand the leftovers back to the free pool - the same thing
    // bulk_insert_prepare does with the tail of the free list it did not need.
    // Returns the input's new end position.
    iter_pos shrink_bulk_copied_input( auto & nodes, size_type const old_size, size_type const new_size ) noexcept
    {
        BOOST_ASSUME( new_size > 0        );
        BOOST_ASSUME( new_size < old_size );
        // NB: `nodes` is sized by node_count_required_for_values(), i.e. for the
        // whole tree - leaves AND the interior levels above them - while only
        // its leaf entries were ever filled in. Derive the leaf count; reading
        // nodes.size() walks into default-initialized pointers.
        auto const old_leaves{ static_cast<std::uint32_t>( ( old_size + leaf_node::max_values - 1 ) / leaf_node::max_values ) };
        auto const last_index{ static_cast<std::uint32_t>( ( new_size - 1 ) / leaf_node::max_values ) };
        auto &     last_leaf { *nodes[ last_index ] };
        last_leaf.num_vals = static_cast<node_size_type>( new_size - ( size_type{ last_index } * leaf_node::max_values ) );
        last_leaf.mark_dirty();
        BOOST_ASSUME( last_leaf.num_vals > 0 );
        // Back to front, so that each node's right link is already cleared by
        // the time it is freed (bptree_base::free wants no dangling backlink).
        for ( auto i{ old_leaves }; i-- > last_index + 1; ) {
            unlink_right     ( *nodes[ i - 1 ] );
            bptree_base::free( *nodes[ i     ] );
        }
        return { slot_of( last_leaf ), last_leaf.num_vals };
    }

    [[ gnu::noinline ]]
    void bulk_insert_into_empty( node_slot const begin_leaf, iter_pos const end_leaf, size_type const total_size )
    {
        BOOST_ASSUME( empty() );
        auto * hdr{ &this->hdr() };
        set_first_leaf( *hdr, begin_leaf    );
        set_last_leaf ( *hdr, end_leaf.node );
        BOOST_ASSUME( hdr->depth_ == 0 );
        if ( begin_leaf == end_leaf.node ) [[ unlikely ]] // single-node-sized initial insert
        {
            BOOST_ASSUME( total_size <= leaf_node::max_values );
            hdr->root_  = begin_leaf;
            hdr->size_  = total_size;
            hdr->depth_ = ( total_size != 0 );
            return;
        }
        auto const & first_root_left { leaf ( begin_leaf      ) };
        auto       & first_root_right{ right( first_root_left ) };
        first_root_right.parent_child_idx = 1;
        hdr->depth_                       = 1;
        // if ( !first_unconnected_node ) then first_root_right is the last leaf
        // and it might be incomplete - we could perform this check&fix in an
        // appropriate else branch below but then we would also have to perform
        // the parent separator key update (like in bulk_append_tail) - so we
        // simply perform it beforehand.
        bulk_append_fill_leaf_if_incomplete( first_root_right );
        auto const first_unconnected_node{ first_root_right.right };
        new_root( begin_leaf, first_root_left.right, key_rv_arg{ /*mrmlj*/Key{ key_at( first_root_right, 0 ) } } ); // may invalidate references
        hdr = &this->hdr();
        BOOST_ASSUME( hdr->depth_ == 2 );
        if ( first_unconnected_node ) { // first check if there are more than two nodes
            bulk_append_tail( &leaf( first_unconnected_node ), { hdr->root_, 1 } );
        }
        BOOST_ASSUME( hdr->last_leaf_ == end_leaf.node || /*in case it got merged*/ hdr->free_list_ == end_leaf.node );
        BOOST_ASSUME( !!hdr->last_leaf_ );
        hdr->size_ = total_size;
    }

    // This function only serves the purpose of maintaining the rule about the
    // minimum number of children per node - that rule is actually only
    // 'academic' (for making sure that the performance/complexity guarantees
    // will be maintained) - the tree would operate correctly even without
    // maintaining that invariant (TODO make this an option).
    enum struct incomplete_resolution { not_incomplete = false, filled, merged_and_freed };
    [[ nodiscard ]]
    incomplete_resolution bulk_append_fill_leaf_if_incomplete( leaf_node * & p_leaf ) noexcept
    {
        if ( p_leaf->num_vals < p_leaf->min_values ) [[ unlikely ]]
        {
            auto const preceding{ p_leaf->left };
            auto const result{ bulk_append_fill_incomplete_leaf( *p_leaf ) };
            switch ( result ) {
                case incomplete_resolution::filled: break;
                case incomplete_resolution::merged_and_freed: p_leaf = &leaf( preceding ); break;
                default: BOOST_UNREACHABLE();
            }
            return result;
        }
        return incomplete_resolution::not_incomplete;
    }
    incomplete_resolution bulk_append_fill_leaf_if_incomplete( leaf_node & leaf ) noexcept
    { // version for callers which known that merge&free should not happen
        auto p_leaf{ &leaf };
        auto const result{ bulk_append_fill_leaf_if_incomplete( p_leaf ) };
        BOOST_ASSUME( result != incomplete_resolution::merged_and_freed );
        BOOST_ASSUME( p_leaf == &leaf );
        return incomplete_resolution::not_incomplete;
    }
    [[ gnu::noinline ]]
    incomplete_resolution bulk_append_fill_incomplete_leaf( leaf_node & leaf ) noexcept
    {
        BOOST_ASSUME( leaf.num_vals < leaf.min_values );
        node_size_type const missing_keys( leaf.min_values - leaf.num_vals );
        auto & preceding{ left( leaf ) };
        if ( preceding.num_vals + leaf.num_vals >= leaf_node::min_values * 2 ) [[ likely ]]
        {
            shift_entries_right( leaf, 0, leaf.num_vals + missing_keys, missing_keys );
            this->move_entries( preceding, preceding.num_vals - missing_keys, preceding.num_vals, leaf, 0 );
            leaf     .num_vals += missing_keys;
            preceding.num_vals -= missing_keys;
            leaf     .mark_dirty();
            preceding.mark_dirty();
            verify_min_max( leaf      );
            verify_min_max( preceding );
            return incomplete_resolution::filled;
        }
        else
        {
            BOOST_ASSUME( !leaf.right );
            append_and_free( preceding, leaf );
            return incomplete_resolution::merged_and_freed;
        }
    }
    // 'pure' bulk append - assumes empty and lone-root scenarios have been handled
    [[ gnu::noinline ]]
    void bulk_append_tail( leaf_node * src_leaf, insert_pos_t rightmost_parent_pos )
    {
        BOOST_ASSERT( src_leaf != static_cast<node_header const *>( &root() ) );
        for ( ;; )
        {
            BOOST_ASSUME( !src_leaf->parent );
            auto const next_src_slot{ src_leaf->right };
            // handle a super edge case: appending 2 nodes to a lone root such
            // that the three nodes together add up to only two nodes minimally
            // filled (resulting in the last one having to be merged and freed)
            if ( next_src_slot ) {
                verify_min_max( *src_leaf );
            } else { // this should only ever happen with the last input node
                if ( bulk_append_fill_leaf_if_incomplete( src_leaf ) == incomplete_resolution::merged_and_freed ) [[ unlikely ]] {
                    break;
                }
            }
            auto & rightmost_parent{ inner( rightmost_parent_pos.node ) };
            BOOST_ASSUME( rightmost_parent_pos.next_insert_offset == rightmost_parent.num_vals );
            auto const src_slot{ slot_of( *src_leaf ) };
            rightmost_parent_pos = insert // add src_leaf into (a) parent
            (
                rightmost_parent,
                rightmost_parent_pos.next_insert_offset,
                key_rv_arg{ /*mrmlj*/Key{ key_at( *src_leaf, 0 ) } },
                src_slot
            );
            if ( !next_src_slot ) {
                src_leaf = &leaf( src_slot );
                break;
            }
            src_leaf = &leaf( next_src_slot );
        }
        set_last_leaf( hdr(), slot_of( *src_leaf ) );
    }
    [[ gnu::noinline ]]
    size_t bulk_append( node_header const & tgt_leaf, leaf_node & src_leaf, size_t const total_insertion_size, iter_pos const end_pos, node_slot const begin_leaf )
    {
        if ( tgt_leaf.is_root() ) [[ unlikely ]]
        {
            // handle the case of a bulk append to a lone root: reuse the
            // bulk_insert_into_empty method by (re)moving the root to the
            // start/left of the bulk of the nodes to be inserted
            auto & hdr{ this->hdr() };
            BOOST_ASSUME( hdr.depth_ == 1 );
            BOOST_ASSUME( hdr.root_  == slot_of( tgt_leaf ) );
            BOOST_ASSUME( hdr.first_leaf_ == hdr.root_ );
            BOOST_ASSUME( hdr.last_leaf_  == hdr.root_ );
            auto const root_slot    { hdr.root_ };
            auto const previous_size{ hdr.size_ };
#       if 0 // these need not hold as the root could have been filled up beyond
            // the minimum prior to any bulk_append_fill_leaf_if_incomplete-like
            // call (e.g. if
            // previous_size + a_source_leaf.num_vals < leaf_node::max_values)
            if ( previous_size < leaf_node::min_values ) { BOOST_ASSUME( tgt_leaf.num_vals == leaf_node::min_values ); } // was filled minimally by a previous step to form a valid leaf_node
            else                                         { BOOST_ASSUME( tgt_leaf.num_vals <= previous_size         ); } // may be less if src_leaf was incomplete and was filled from the root node
#       endif
            hdr.root_ = hdr.first_leaf_ = hdr.last_leaf_ = {};
            hdr.size_ = hdr.depth_ = 0;
            BOOST_ASSUME( tgt_leaf.right == begin_leaf );
            BOOST_ASSUME( previous_size <= leaf_node::max_values );
            bulk_insert_into_empty( root_slot, end_pos, previous_size + total_insertion_size );
            BOOST_ASSUME( this->hdr().size_ == previous_size + total_insertion_size );
        }
        else
        {
            auto const rightmost_parent_slot{ tgt_leaf.parent };
            node_size_type const parent_pos { static_cast<node_size_type>( tgt_leaf.parent_child_idx ) }; // key idx = child idx - 1 & this is the 'next' key
            bulk_append_tail( &src_leaf, { rightmost_parent_slot, parent_pos } );
            this->hdr().size_ += total_insertion_size;
        }
        // courtesy return for tail calls
        return total_insertion_size;
    }

    [[ gnu::pure ]]  leaf_node & leaf  ( node_slot const slot ) noexcept { return node< leaf_node>( slot ); }
    [[ gnu::pure ]] inner_node & inner ( node_slot const slot ) noexcept { return node<inner_node>( slot ); }
    [[ gnu::pure ]] inner_node & parent( node_header & child ) noexcept { return inner( child.parent ); }

     leaf_node const & leaf  ( node_slot   const   slot  ) const noexcept { return const_cast<bptree_base_wkey &>( *this ).leaf ( slot ); }
    inner_node const & inner ( node_slot   const   slot  ) const noexcept { return const_cast<bptree_base_wkey &>( *this ).inner( slot ); }
    inner_node const & parent( node_header const & child ) const noexcept { return const_cast<bptree_base_wkey &>( *this ).parent( const_cast<node_header &>( child ) ); }

    // new separator specified separately to support both use cases (pre or post
    // change of the node itself)
    void update_separator( leaf_node & leaf, Key const & new_separator ) noexcept
    {
        // the leftmost leaf does not have a separator key (at all)
        if ( !leaf.left ) [[ unlikely ]]
        {
            BOOST_ASSUME( leaf.parent_child_idx == 0 );
            BOOST_ASSUME( hdr().first_leaf_ == slot_of( leaf ) );
            return;
        }
        // a leftmost child does not have a key in the immediate parent
        // (because a left child is strictly less-than its separator key - for
        // the leftmost child there is no key further left that could be
        // greater-or-equal to it) so we have to search further up the ancestors
        auto   parent_child_idx{ leaf.parent_child_idx };
        auto * parent          { &this->parent( leaf ) };
        while ( parent_child_idx == 0 )
        {
            parent_child_idx = parent->parent_child_idx;
            parent           = &this->parent( *parent );
        }
        // can be zero only for the leftmost leaf which was checked for in the
        // loop above and at the beginning of the function
        BOOST_ASSUME( parent_child_idx > 0 );
        auto & parent_key{ key_at( *parent, parent_child_idx - 1 ) };
        parent_key = new_separator;
        parent->mark_dirty();
    }
    void update_separator( leaf_node & leaf ) noexcept { update_separator( leaf, key_at( leaf, 0 ) ); }

    template <typename N>
    [[ gnu::noinline, gnu::sysv_abi ]]
    iter_pos handle_underflow( N & node ) noexcept
    {
        BOOST_ASSUME( underflowed( node ) );
        BOOST_ASSUME( !node.is_root() );

        auto constexpr parent_node_type{ requires{ node.children; } };
        auto constexpr leaf_node_type  { !parent_node_type };

        auto const this_slot{ slot_of( node ) };
        auto & parent{ inner( node.parent ) };
        verify( parent );

        // in nonunique instances more than one key (equivalent copy) can be
        // erased at once (with num_vals potentially dropping below min_vals-1,
        // i.e. missing_values can be > 1)
        //BOOST_ASSUME( node.num_vals == node.min_values - 1 || !unique );
        BOOST_ASSUME( node.num_vals > 0 );
        BOOST_ASSUME( node.num_vals < node.min_values );
        node_size_type const missing_values( node.min_values - node.num_vals );

        // It may happen that a leaf's separator key appears not in the
        // immediate parent but further up - which would require a more complex
        // (recursive climb) logic to update it (like other functions which call
        // update_separator) - however this is not in fact necessary since:
        // - this may happen only for the leftmost children (leaves) of a given
        //   immediate parent (lowest inner node)
        // - handle_underflow does not delete values, it may only change them in
        //   the sense of moving them around
        // - the only way for a leftmost value to change (get moved) is for its
        //   containing node to borrow from or to its left sibling
        // - the 'problematic' nodes in question here are the leftmost nodes
        //   i.e. ones which do _not have_ a left sibling
        // - the second way would be through merging but we always do
        //   right-to-left merging (which leaves the leftmost value of the left
        //   node intact).
        // Therefore this case can be ignored/can never happen in
        // handle_overflow for leaves (IOW WRT this leaves do not require
        // different handling compared to inner nodes).
        auto const parent_child_idx   { node.parent_child_idx };
        bool const parent_has_key_copy{ leaf_node_type && ( parent_child_idx > 0 ) };
        auto const parent_key_idx     { parent_child_idx - parent_has_key_copy };
        BOOST_ASSUME( !parent_has_key_copy || key_at( parent, parent_key_idx ) == key_at( node, 0 ) );

        BOOST_ASSUME( parent.children[ parent_child_idx ] == this_slot );
        // the left and right level dlink pointers can point 'across' parents
        // (and so cannot be used to resolve the existence of siblings)
        auto const has_right_sibling{ parent_child_idx < ( num_chldrn( parent ) - 1 ) };
        auto const has_left_sibling { parent_child_idx > 0 };
        auto const p_right_sibling  { has_right_sibling ? &right( node ) : nullptr };
        auto const p_left_sibling   { has_left_sibling  ? &left ( node ) : nullptr };

        // save&return the node (and offset) that the underflowed node's values
        // end up
        auto           final_node                     { this_slot };
        node_size_type final_node_original_keys_offset{ 0 };

        BOOST_ASSUME( has_right_sibling || has_left_sibling );
        BOOST_ASSERT( &node != p_left_sibling  );
        BOOST_ASSERT( &node != p_right_sibling );
        BOOST_ASSERT( static_cast<node_header *>( &node ) != static_cast<node_header *>( &parent ) );
        // Borrow from left sibling if possible
        if ( p_left_sibling && can_borrow( *p_left_sibling ) )
        {
            verify_min_max( *p_left_sibling );
            node.num_vals++;
            if constexpr ( requires { node.children; } ) {
                rshift_entries( node );
            } else {
                // this is the borrow the front gap exists for: taking one entry
                // in at the front is just where the entries now begin
                if ( node.start ) --node.start;
                else              rshift_entries( node );
            }
            node_size_type const left_separator_key_idx( parent_child_idx - 1 );
            auto & left_separator_key{ keys( parent )[ left_separator_key_idx ] };
            auto const node_keys{ keys( node ) };
            auto const left_keys{ keys( *p_left_sibling ) };
            if constexpr ( leaf_node_type ) {
                // Move the largest key from left sibling to the current node
                BOOST_ASSUME( parent_has_key_copy );
                node_keys.front() = std::move( left_keys.back() );
                // adjust the separator key in the parent
                BOOST_ASSERT( left_separator_key == node_keys[ 1 ] );
                left_separator_key = node_keys.front();
            } else {
                // Move/rotate the largest key from left sibling to the current node 'through' the parent

                // no comparator in base classes :/ (also would need adjustments for non-unique support)
                //BOOST_ASSERT( lt( left_keys.back()  , left_separator_key ) );
                //BOOST_ASSERT( lt( left_separator_key, node_keys.front()  ) );
                node_keys.front()  = std::move( left_separator_key );
                left_separator_key = std::move( left_keys.back() );

                rshift_chldrn( node );
                insrt_child( node, 0, children( *p_left_sibling ).back(), this_slot );
            }

            p_left_sibling->num_vals--;
            node.mark_dirty();
            parent.mark_dirty();
            p_left_sibling->mark_dirty();
            verify_min_max( *p_left_sibling );

            final_node_original_keys_offset = 1;

            BOOST_ASSUME( node.           num_vals == N::min_values - ( missing_values - 1 ) );
            BOOST_ASSUME( p_left_sibling->num_vals >= N::min_values );
        }
        // Borrow from right sibling if possible
        else
        if ( p_right_sibling && can_borrow( *p_right_sibling ) )
        {
            verify_min_max( *p_right_sibling );
            node.num_vals++;
            auto const right_separator_key_idx{ parent_child_idx };
            auto & right_separator_key{ keys( parent )[ right_separator_key_idx ] };
            auto const node_keys{ keys( node ) };
            if constexpr ( leaf_node_type ) {
                // Move the smallest key from the right sibling to the current node
                auto & leftmost_right_key{ keys( *p_right_sibling ).front() };
                BOOST_ASSUME( right_separator_key == leftmost_right_key ); // yes we expect exact or bitwise equality for key-copies in inner nodes
                node_keys.back() = std::move( leftmost_right_key );
                lshift_entries( *p_right_sibling );
                // adjust the separator key in the parent
                right_separator_key = leftmost_right_key;
            } else {
                // Move/rotate the smallest key from the right sibling to the current node 'through' the parent

                // no comparator in base classes :/ (also would need adjustments for non-unique support)
                //BOOST_ASSUME( lt( key_at( parent, parent_child_idx ), key_at( *p_right_sibling, 0 ) ) );
                //BOOST_ASSERT( lt( *( node_keys.end() - 2 ), right_separator_key ) );
                node_keys.back()    = std::move( right_separator_key );
                right_separator_key = std::move( keys( *p_right_sibling ).front() );
                insrt_child( node, num_chldrn( node ) - 1, children( *p_right_sibling ).front(), this_slot );
                lshift_entries  ( *p_right_sibling );
                lshift_chldrn( *p_right_sibling );
            }

            p_right_sibling->num_vals--;
            node.mark_dirty();
            parent.mark_dirty();
            p_right_sibling->mark_dirty();
            verify_min_max( *p_right_sibling );

            BOOST_ASSUME( node.            num_vals == N::min_values - ( missing_values - 1 ) );
            BOOST_ASSUME( p_right_sibling->num_vals >= N::min_values );
        }
        // Merge with left or right sibling
        else
        {
            if ( p_left_sibling ) {
                // need not hold for nonunique trees&bulk erase underflow
                //verify_min_max( *p_left_sibling );
				verify( *p_left_sibling );
                BOOST_ASSUME( parent_has_key_copy == leaf_node_type );
                // Merge node -> left sibling
                final_node                      = node.left;
                final_node_original_keys_offset = p_left_sibling->num_vals;
                merge_right_into_left( parent, *p_left_sibling, node );
            } else {
                verify_min_max( *p_right_sibling );
                BOOST_ASSUME( parent_key_idx == 0 );
                // no comparator in base classes :/
                //BOOST_ASSUME( le( key_at( parent, parent_key_idx ), key_at( *p_right_sibling, 0 ) ) );
                // Merge right sibling -> node
                merge_right_into_left( parent, node, *p_right_sibling );
            }
        }

        return { final_node, final_node_original_keys_offset };
    } // handle_underflow()

    root_node       & root()       noexcept { return as<root_node>( bptree_base::root() ); }
    root_node const & root() const noexcept { return const_cast<bptree_base_wkey &>( *this ).root(); }

    using bptree_base::free;
    void free( leaf_node & leaf ) noexcept { bptree_base::free_leaf( leaf ); }

    using bptree_base::unlink_and_free_node;
    void unlink_and_free_node( leaf_node & leaf, leaf_node & cached_left_sibling ) noexcept { bptree_base::unlink_and_free_leaf( leaf, cached_left_sibling ); }

    template <typename N>
    [[ gnu::sysv_abi ]] static
    void move_entries
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
        child.parent_child_idx = pos;
        child.mark_dirty();
    }
    void insrt_child( inner_node & target, node_size_type const pos, node_slot const child_slot ) noexcept
    {
        insrt_child( target, pos, child_slot, slot_of( target ) );
    }

    void append_and_free( leaf_node & __restrict target, leaf_node & __restrict source ) noexcept
    {
        // The append lands at key_at( target, num_vals ), i.e. physical
        // start + num_vals, while the bound below covers num_vals alone - so a
        // target carrying a front gap is written past the end of its array by
        // exactly 'start'.  Witnessed: start=29, num_vals=123, max_values=123.
        recentre( target );
        BOOST_ASSUME( target.num_vals + source.num_vals <= target.max_values );

        std::ranges::move( keys( source ), &key_at( target, target.num_vals ) );
        target.num_vals += source.num_vals;
        source.num_vals  = 0;
        target.mark_dirty();

        // need not hold for nonunique trees&bulk erase underflow
        //verify_min_max( target );
		verify( target );
        unlink_and_free_node( source, target );
        // does not assume that there is nothing right of source (e.g. could be
        // an in-the-middle removal with underflow)
    }

    void merge_right_into_left
    (
        inner_node & __restrict parent,
         leaf_node & __restrict left, leaf_node & __restrict right
    ) noexcept
    {
#   if 0 // need not hold for nonunique trees
        auto constexpr min{ leaf_node::min_values };
        BOOST_ASSUME( right.num_vals >= min - 1 ); BOOST_ASSUME( right.num_vals <= min );
        BOOST_ASSUME( left .num_vals >= min - 1 ); BOOST_ASSUME( left .num_vals <= min );
#   endif
        BOOST_ASSUME( left .right == slot_of( right ) );
        BOOST_ASSUME( right.left  == slot_of( left  ) );
        auto const parent_child_idx{ right.parent_child_idx };
        append_and_free( left, right );
        remove_from_parent( parent, parent_child_idx );
    }

    void merge_right_into_left
    (
        inner_node & __restrict parent,
        inner_node & __restrict left, inner_node & __restrict right
    ) noexcept
    {
        auto constexpr min{ inner_node::min_values };
        BOOST_ASSUME( right.num_vals >= min - 1 ); BOOST_ASSUME( right.num_vals <= min );
        BOOST_ASSUME( left .num_vals >= min - 1 ); BOOST_ASSUME( left .num_vals <= min );

        move_chldrn( right, 0, num_chldrn( right ), left, num_chldrn( left ) );
        auto const parent_key_idx{ right.parent_child_idx - 1 };
        auto & separator_key{ key_at( parent, parent_key_idx ) };
        left.num_vals += 1;
        auto & last_left_key{ keys( left ).back() };
        last_left_key = std::move( separator_key );
        std::ranges::move( keys( right ), std::next( &last_left_key ) );
        left.num_vals += right.num_vals;
        left.mark_dirty();
        BOOST_ASSUME( left.num_vals >= left.max_values - 1 ); BOOST_ASSUME( left.num_vals <= left.max_values );

        verify_min_max( left );
        remove_from_parent( parent, right.parent_child_idx );
        unlink_and_free_node( right, left );
    }

    static auto copy_n( leaf_node const & lf, node_size_type const offset, node_size_type const count, auto output, auto && proj ) noexcept( std::is_nothrow_invocable_v<decltype( proj ) &, Key const &> )
    {
        if constexpr ( std::is_same_v<std::remove_cvref_t<decltype( proj )>, std::identity> ) {
            return std::copy_n( &key_at( lf, offset ), count, output );
        } else {
            // std::invoke so any std::invocable projection works (lambdas,
            // function pointers, pointer-to-member, std::reference_wrapper…) —
            // std::transform's third-argument invocation path would only accept
            // plain `proj(x)` forms.
            auto const end{ &key_at( lf, offset + count ) };
            for ( auto const * p{ &key_at( lf, offset ) }; p != end; ++p ) {
                *output++ = std::invoke( proj, *p );
            }
            return output;
        }
    }

    static void verify( auto const & node ) noexcept
    {
        //...mrmlj...need not hold for nonunique trees
        //BOOST_ASSERT( std::ranges::adjacent_find( keys( node ) ) == keys( node ).end() );
        bptree_base::verify( node );
    }

private:
    [[ gnu::const, gnu::noinline ]]
    static node_slot::value_type node_count_required_for_values( size_type const number_of_values ) noexcept
    {
        if ( number_of_values <= leaf_node::max_values )
            return ( number_of_values != 0 );
        auto const  leaf_count{ static_cast<node_slot::value_type>( divide_up( number_of_values, /*assuming an 'optimistic' reserve, i.e. for bulk insert*/leaf_node::max_values ) ) };
        auto       total_count{ leaf_count };
        auto       current_level_count{ leaf_count };
        auto       depth{ 1 };
        while ( current_level_count > 1 )
        {
            current_level_count = divide_up( current_level_count, inner_node::min_children ); // pessimistic about inner node utilization
            total_count += current_level_count;
            ++depth;
        }
        // +1 since we use a 1-based depth index (instead of a 0-based where -1
        // is used to denote an empty tree)
        auto const minimum_height{ static_cast<std::uint8_t>( 1 + std::ceil( std::log( leaf_count ) / std::log( inner_node::max_children ) ) ) };
        auto const maximum_height{ static_cast<std::uint8_t>( 1 + std::ceil( std::log( leaf_count ) / std::log( inner_node::min_children ) ) ) };
        BOOST_ASSUME( depth >= minimum_height );
        BOOST_ASSUME( depth <= maximum_height );
        [[ maybe_unused ]]
        auto tree_structure_overhead{ total_count - leaf_count };
        return total_count;
    }

    template <typename Proj = std::identity>
    auto flatten( node_slot begin_node, node_slot end_node, std::output_iterator<std::invoke_result_t<Proj &, Key const &>> auto output, Proj proj = {} ) const noexcept( std::is_nothrow_invocable_v<Proj &, Key const &> );
}; // class bptree_base_wkey

////////////////////////////////////////////////////////////////////////////////
// \class bptree_base_wkey::fwd_iterator
////////////////////////////////////////////////////////////////////////////////

template <typename Key>
class [[ clang::trivial_abi, gsl::Pointer ]] bptree_base_wkey<Key>::fwd_iterator
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
        return key_at( leaf, pos_.value_offset );
    }

    std::span<Key const> get_contiguous_span_and_move_to_next_node() noexcept
    {
        auto & leaf{ static_cast<leaf_node &>( node() ) };
        BOOST_ASSUME( pos_.value_offset < leaf.num_vals );
        std::span<Key const> const span{ &key_at( leaf, pos_.value_offset ), leaf.num_vals - pos_.value_offset };
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
class [[ clang::trivial_abi, gsl::Pointer ]] bptree_base_wkey<Key>::ra_iterator
    :
    public base_random_access_iterator,
    public iter_impl<ra_iterator, std::random_access_iterator_tag>
{
private: friend class bptree_base_wkey<Key>;
    using base = base_random_access_iterator;
    using base::base;

    leaf_node & node() const noexcept { return static_cast<leaf_node &>( base::node() ); }

public:
    constexpr ra_iterator() noexcept = default;

    // TODO deduplicate this w/ fwd_iterator
    Key & operator*() const noexcept
    {
        auto & leaf{ node() };
        BOOST_ASSUME( pos_.value_offset < leaf.num_vals );
        return key_at( leaf, pos_.value_offset );
    }

    std::span<Key const> get_contiguous_span_and_move_to_next_node() noexcept
    {
        auto & leaf{ static_cast<leaf_node &>( node() ) };
        BOOST_ASSUME( pos_.value_offset < leaf.num_vals );
        std::span<Key const> const span{ &key_at( leaf, pos_.value_offset ), leaf.num_vals - pos_.value_offset };
        index_            += span.size();
        pos_.node          = leaf.right;
        pos_.value_offset  = 0;
        return span;
    }

    ra_iterator   operator+ ( difference_type const n ) const noexcept { return static_cast<ra_iterator &&>( base::operator+ (  n ) ); }
    ra_iterator & operator+=( difference_type const n )       noexcept { return static_cast<ra_iterator & >( base::operator+=(  n ) ); }
    ra_iterator   operator- ( difference_type const n ) const noexcept { return static_cast<ra_iterator &&>( base::operator+ ( -n ) ); }
    using base::operator-;

    ra_iterator & operator++(   ) noexcept { return static_cast<ra_iterator & >( base::operator++( ) ); }
    ra_iterator   operator++(int) noexcept { return static_cast<ra_iterator &&>( base::operator++(0) ); }
    ra_iterator & operator--(   ) noexcept { return static_cast<ra_iterator & >( base::operator--( ) ); }
    ra_iterator   operator--(int) noexcept { return static_cast<ra_iterator &&>( base::operator--(0) ); }

    friend constexpr auto operator<=>( ra_iterator const & left, ra_iterator const & right ) noexcept { return static_cast<base const &>( left ) <=> static_cast<base const &>( right ); }
    friend constexpr bool operator== ( ra_iterator const & left, ra_iterator const & right ) noexcept { return static_cast<base const &>( left ) ==  static_cast<base const &>( right ); }

    operator fwd_iterator() const noexcept { return static_cast<fwd_iterator const &>( static_cast<base_iterator const &>( *this ) ); }
}; // class ra_iterator


template <typename Key>
class [[ clang::trivial_abi ]] bptree_base_wkey<Key>::ra_full_node_iterator
    // Not using stl_interfaces because Clang 19.1.6 under OSX keeps using the
    // stl_interfaces implementations/wrappers for equality operators (even
    // though proper class specific ones are provided - as members, friends,
    // plain globals, nothing helps) which in turn produce UBSan noise.
    //:public iter_impl<ra_full_node_iterator, std::random_access_iterator_tag>
{
public:
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept  = std::random_access_iterator_tag;
    using value_type        = Key;
    using reference         = Key &;
    using pointer           = Key *;

    // Have to provide default construction in order to model
    // std::random_access_iterator (yet at the same time do not want to in order
    // to be able to omit the check in the assignment operator as is required
    // for base_iterator).
    constexpr ra_full_node_iterator() noexcept { std::unreachable(); }
    constexpr ra_full_node_iterator( leaf_node * const leaves[], size_type const value_index ) noexcept
        : leaves_{ leaves }, value_index_{ value_index } { refresh_cache(); }
    ra_full_node_iterator( ra_full_node_iterator const & ) = default;

    reference operator*() const noexcept
    {
        static_assert( std::random_access_iterator<ra_full_node_iterator> );
        // Lazy leaf load: refresh_cache() and operator++ leaf-crossing set
        // cached_leaf_ to nullptr because the iterator position may be end()
        // (cached_node_index_ == leaf_count, i.e. one-past-last — reading
        // leaves_[cached_node_index_] would be OOB).  By the time we get here
        // the caller is actually dereferencing, so the position is valid and
        // the load is safe.  On the hot steady-state path the branch is not
        // taken (cached_leaf_ was filled by a prior deref or by operator--).
        if ( !cached_leaf_ ) [[ unlikely ]] {
            cached_leaf_ = leaves_[ cached_node_index_ ];
        }
        BOOST_ASSUME( cached_offset_ < leaf_node::max_values );
        return key_at( *cached_leaf_, cached_offset_ );
    }
    reference operator[]( difference_type const n ) const noexcept { return *(*(this) + n); }

    PSI_WARNING_DISABLE_PUSH()
    PSI_WARNING_GCC_OR_CLANG_DISABLE( -Wsign-conversion )
    ra_full_node_iterator   operator+ ( difference_type const n ) const noexcept { return { leaves_, value_index_ + n }; }
    ra_full_node_iterator & operator+=( difference_type const n )       noexcept { value_index_ += n; refresh_cache(); return *this; }
    ra_full_node_iterator   operator- ( difference_type const n ) const noexcept { return { leaves_, value_index_ - n }; }
    ra_full_node_iterator & operator-=( difference_type const n )       noexcept { value_index_ -= n; refresh_cache(); return *this; }
    PSI_WARNING_DISABLE_POP()
    friend ra_full_node_iterator operator+( difference_type const n, ra_full_node_iterator const iter ) noexcept { return iter + n; }

    ra_full_node_iterator & operator++() noexcept
    {
        ++value_index_;
        if ( ++cached_offset_ == leaf_node::max_values ) [[ unlikely ]] {
            ++cached_node_index_;
            // Do not read leaves_[cached_node_index_] here: the ++ may have
            // advanced to end() (cached_node_index_ == leaf_count), in which
            // case that slot is OOB.  operator* reloads lazily when (and if)
            // the caller actually dereferences.
            cached_leaf_   = nullptr;
            cached_offset_ = 0;
        }
        return *this;
    }
    ra_full_node_iterator operator++(int) noexcept { auto const tmp{ *this }; ++*this; return tmp; }
    ra_full_node_iterator & operator--() noexcept
    {
        --value_index_;
        if ( cached_offset_ == 0 ) [[ unlikely ]] {
            --cached_node_index_;
            cached_leaf_   = leaves_[ cached_node_index_ ];
            cached_offset_ = leaf_node::max_values - 1;
        } else {
            --cached_offset_;
        }
        return *this;
    }
    ra_full_node_iterator operator--(int) noexcept { auto const tmp{ *this }; --*this; return tmp; }

    [[ gnu::const ]] constexpr auto            operator<=>( this ra_full_node_iterator const self, ra_full_node_iterator const other ) noexcept { BOOST_ASSUME( self.leaves_ == other.leaves_ ); return self.value_index_ <=> other.value_index_; }
    [[ gnu::const ]] constexpr bool            operator== ( this ra_full_node_iterator const self, ra_full_node_iterator const other ) noexcept { BOOST_ASSUME( self.leaves_ == other.leaves_ ); return self.value_index_ ==  other.value_index_; }
    [[ gnu::const ]] constexpr difference_type operator-  ( this ra_full_node_iterator const self, ra_full_node_iterator const other ) noexcept { BOOST_ASSUME( self.leaves_ == other.leaves_ ); return static_cast<difference_type>( self.value_index_ - other.value_index_ ); }

    ra_full_node_iterator & operator=( ra_full_node_iterator const & other ) noexcept
    {
        BOOST_ASSUME( this->leaves_ == other.leaves_ );
        this->value_index_       = other.value_index_;
        this->cached_leaf_       = other.cached_leaf_;
        this->cached_node_index_ = other.cached_node_index_;
        this->cached_offset_     = other.cached_offset_;
        return *this;
    }

private:
    constexpr void refresh_cache() noexcept
    {
        cached_node_index_ = static_cast<std::uint32_t >( value_index_ / leaf_node::max_values );
        cached_offset_     = static_cast<node_size_type>( value_index_ % leaf_node::max_values );
        // Do not eager-load leaves_[cached_node_index_] here: the constructor
        // (and operator+=/-=) may be forming the end() position, where
        // cached_node_index_ == leaf_count (one-past-last) and the read would
        // be OOB.  operator* fills cached_leaf_ lazily on first actual deref.
        cached_leaf_       = nullptr;
    }

    leaf_node * const * const leaves_           {};
    size_type                 value_index_      {};
    // Cache (lazily synchronized with value_index_): avoids per-deref div/mod
    // by non-power-of-2 max_values.  Updated on ++/-- via cheap inc/dec + rare
    // leaf crossing; rebuilt from scratch on +=n / -=n.  cached_leaf_ is
    // nullptr when the cached (node_index, offset) may be past-the-end — it
    // is filled on demand by operator*.
    mutable leaf_node *       cached_leaf_      {};
    std::uint32_t             cached_node_index_{};
    node_size_type            cached_offset_    {};
}; // class ra_full_node_iterator


////////////////////////////////////////////////////////////////////////////////
// \class bptree_base_wkey::leaf_iterator
////////////////////////////////////////////////////////////////////////////////
// Bidirectional iterator over the doubly-linked list of leaf nodes: dereferences
// to std::span<Key const> of the leaf's keys.  Enables two-level loops that
// skip the per-step pos_ bookkeeping inside fwd_iterator.
template <typename Key>
class [[ clang::trivial_abi, gsl::Pointer ]] bptree_base_wkey<Key>::leaf_iterator
{
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept  = std::bidirectional_iterator_tag;
    using value_type        = std::span<Key const>;
    using reference         = std::span<Key const>;
    using difference_type   = std::ptrdiff_t;
    using pointer           = void;

    constexpr leaf_iterator() noexcept = default;
    constexpr leaf_iterator( bptree_base_wkey const & tree, leaf_node const * const lf ) noexcept
        : p_leaf_{ lf }, p_tree_{ &tree } {}

    [[ gnu::pure ]]
    std::span<Key const> operator*() const noexcept
    {
        BOOST_ASSUME( p_leaf_ );
        BOOST_ASSUME( p_leaf_->num_vals <= leaf_node::max_values );
        return { &bptree_base::key_at( *p_leaf_, 0 ), p_leaf_->num_vals };
    }

    leaf_iterator & operator++() noexcept
    {
        BOOST_ASSUME( p_leaf_ );
        p_leaf_ = BOOST_LIKELY( bool( p_leaf_->right ) ) ? &p_tree_->leaf( p_leaf_->right ) : nullptr;
        return *this;
    }
    leaf_iterator operator++( int ) noexcept { auto const tmp{ *this }; ++*this; return tmp; }

    // Decrementing end() yields the last leaf; decrementing begin() is
    // undefined (matches std::bidirectional_iterator contract).
    leaf_iterator & operator--() noexcept
    {
        if ( !p_leaf_ ) [[ unlikely ]] { // end -> last leaf
            BOOST_ASSUME( !p_tree_->empty() );
            p_leaf_ = &p_tree_->leaf( p_tree_->hdr().last_leaf_ );
        } else {
            BOOST_ASSUME( bool( p_leaf_->left ) );
            p_leaf_ = &p_tree_->leaf( p_leaf_->left );
        }
        return *this;
    }
    leaf_iterator operator--( int ) noexcept { auto const tmp{ *this }; --*this; return tmp; }

    // Order by the leaf's first key, i.e. by sequence/iteration order.  end()
    // (null p_leaf_) sorts after every real leaf.  Weak ordering because in a
    // non-unique tree two distinct leaves may share the same first key and
    // therefore compare equivalent here -- operator== (defaulted pointer
    // compare) still distinguishes them.  Raw-pointer ordering on p_leaf_
    // would be stable but meaningless (allocation order), so it's not exposed.
    [[ gnu::pure ]] friend std::weak_ordering operator<=>( leaf_iterator const & lhs, leaf_iterator const & rhs ) noexcept
    {
        BOOST_ASSUME( lhs.p_tree_ == rhs.p_tree_ );
        if ( !lhs.p_leaf_ ) return rhs.p_leaf_ ? std::weak_ordering::greater : std::weak_ordering::equivalent;
        if ( !rhs.p_leaf_ ) return std::weak_ordering::less;
        BOOST_ASSUME( lhs.p_leaf_->num_vals > 0 );
        BOOST_ASSUME( rhs.p_leaf_->num_vals > 0 );
        return bptree_base::key_at( *lhs.p_leaf_, 0 ) <=> bptree_base::key_at( *rhs.p_leaf_, 0 );
    }
    [[ gnu::pure ]] friend bool operator==( leaf_iterator const & lhs, leaf_iterator const & rhs ) noexcept { BOOST_ASSUME( lhs.p_tree_ == rhs.p_tree_ ); return lhs.p_leaf_ == rhs.p_leaf_; }

private:
    leaf_node        const * __restrict p_leaf_{};
    bptree_base_wkey const * __restrict p_tree_{};
}; // class leaf_iterator

template <typename Key>
typename bptree_base_wkey<Key>::leaf_iterator
bptree_base_wkey<Key>::node_begin() const noexcept
{
    return { *this, empty() ? nullptr : &leaf( first_leaf() ) };
}

template <typename Key>
typename bptree_base_wkey<Key>::leaf_iterator
bptree_base_wkey<Key>::node_end() const noexcept
{
    return { *this, nullptr };
}


template <typename Key>
typename
bptree_base_wkey<Key>::const_iterator
bptree_base_wkey<Key>::erase( const_iterator const iter ) noexcept
{
    auto const [node, key_offset]{ iter.base().pos() };
    auto & lf{ leaf( node ) };
    if ( key_offset == 0 ) [[ unlikely ]] {
        static_assert( leaf_node::min_values > 1 ); // makes this simpler to handle: we can assume that key_at( lf, 1 ) exists, TODO reconsider the nonunique case
        update_separator( lf, key_at( lf, 1 ) );
    }
    return make_iter( erase( lf, key_offset ) );
}

template <typename Key>
typename
bptree_base_wkey<Key>::const_iterator
bptree_base_wkey<Key>::erase( const_iterator const first, const_iterator const last ) noexcept
{
    auto const end_pos{ last.base().pos() };
    auto pos{ first.base().pos() };
    if ( pos == end_pos ) [[ unlikely ]]
        return last;

    if ( pos.value_offset != 0 )
    {
        auto & node{ leaf( pos.node ) };
        auto const single_node_bulk_erase{ pos.node == end_pos.node };
        auto const node_end_offset{ single_node_bulk_erase ? end_pos.value_offset : node.num_vals };
        auto const erased_count{ static_cast<node_size_type>( node_end_offset - pos.value_offset ) };
        shift_entries_left( node, pos.value_offset, node.num_vals, erased_count );
        node.num_vals -= erased_count;
        node.mark_dirty();
        if ( single_node_bulk_erase ) {
            auto new_pos{ check_and_handle_bulk_erase_underflow( node ) };
            new_pos.value_offset += pos.value_offset;
            return make_iter( new_pos );
        }
        pos = { node.right, 0 };
    }

    while ( pos != end_pos )
    {
        BOOST_ASSUME( pos.value_offset == 0 );
        auto & node{ leaf( pos.node ) };
        if ( pos.node == end_pos.node ) 
        {
            pos.value_offset = end_pos.value_offset;
            if ( end_pos.value_offset < node.num_vals ) // partial, certainly last, node
            {
                auto const erased_count{ end_pos.value_offset };
                shift_entries_left( node, 0, node.num_vals, erased_count );
                node.num_vals -= erased_count;
                node.mark_dirty();
                // erasure not to the end but from the beginning of the node -
                // this also means we've reached the end of the erasure loop
                // (i.e. no more keys to erase)
                this->update_separator                     ( node );
                this->check_and_handle_bulk_erase_underflow( node );
                break;
            }
        } else {
            pos.node = node.right;
        }
        // entire node erased
        this->remove_from_parent  ( node );
        this->unlink_and_free_node( node, this->left( node ) );
    }

    // handling of possible underflow of the starting node is delayed to avoid
    // constant moving/refilling of values from succeeding right leaves - rather
    // this case is handled faster by removing entire same-valued nodes (in case
    // there are any) and then the starting and ending, potentially partially
    // erased, leaves are handled for possible underflow
    this->check_and_handle_bulk_erase_underflow( leaf( first.base().pos().node ) );
    // pos cannot point to the starting node here as that case is handled at the
    // beginning of the function so no need to check/use the return of the above
    // call

    return make_iter( pos );
}

template <typename Key>
template <typename Proj>
auto bptree_base_wkey<Key>::flatten( node_slot const begin_node, node_slot const end_node, std::output_iterator<std::invoke_result_t<Proj &, Key const &>> auto output, Proj proj ) const noexcept( std::is_nothrow_invocable_v<Proj &, Key const &> ) {
    auto node{ begin_node };
    do {
        auto const & lf{ leaf( node ) };
        output = copy_n( lf, 0, lf.num_vals, output, proj );
        node   = lf.right;
    } while ( node != end_node );
    return output;
}

template <typename Key>
template <typename Proj>
auto bptree_base_wkey<Key>::flatten( std::output_iterator<std::invoke_result_t<Proj &, Key const &>> auto const output, size_type const available_space, Proj proj ) const noexcept( std::is_nothrow_invocable_v<Proj &, Key const &> ) {
    BOOST_VERIFY( available_space >= this->size() );
    if ( empty() ) [[ unlikely ]]
        return output;
    BOOST_ASSERT( !leaf( hdr().last_leaf_ ).right ); // broken last_leaf (should have null-right sibling)
    return flatten( first_leaf(), {}, output, std::move( proj ) );
}

template <typename Key>
template <typename Proj>
auto bptree_base_wkey<Key>::flatten( const_iterator const begin, const_iterator const end, std::output_iterator<std::invoke_result_t<Proj &, Key const &>> auto output, size_type available_space, Proj proj ) const noexcept( std::is_nothrow_invocable_v<Proj &, Key const &> ) {
    BOOST_ASSERT( available_space >= static_cast<std::size_t>( std::distance( begin, end ) ) );
    auto const   end_pos{   end.base().pos() };
    auto       start_pos{ begin.base().pos() };
    if ( start_pos.value_offset ) // handle leading partial node
    {
        auto const & lf{ leaf( start_pos.node ) };
        BOOST_ASSUME( start_pos.value_offset <= lf.num_vals );
        if ( start_pos.value_offset == lf.num_vals ) {
            BOOST_ASSUME( start_pos == end_pos );
        }
        auto const start_node_is_end_node{ start_pos.node == end_pos.node };
        node_header::size_type const copy_end { start_node_is_end_node ? end_pos.value_offset : static_cast<node_header::size_type>( lf.num_vals ) };
        node_header::size_type const copy_size( copy_end - start_pos.value_offset );
        BOOST_ASSUME( copy_size <= available_space );
        output = copy_n( lf, start_pos.value_offset, copy_size, output, proj );
        if ( copy_size == available_space ) { // single (partial) node data
            // not simply testing start_node_is_end_node as it does not cover
            // the edge case of where start_pos is the last value of a node and
            // end_pos is the very first value of the next node
            return output;
        }
        BOOST_ASSUME( copy_size < available_space );
        available_space        -= copy_size;
        start_pos.value_offset += copy_size;
        BOOST_ASSUME( start_pos.value_offset == lf.num_vals );
        start_pos = { lf.right, 0 };
    }

    if ( start_pos.node != end_pos.node ) {
        output = flatten( start_pos.node, end_pos.node, output, proj );
    }

    if ( end_pos.node ) // handle trailing partial node
    {
        auto const & lf{ leaf( end_pos.node ) };
        output = copy_n( lf, 0, end_pos.value_offset, output, proj );
    }

    return output;
}

template <typename Key>
template <typename N> [[ gnu::sysv_abi ]]
void bptree_base_wkey<Key>::move_entries
(
    N const & source, node_size_type const src_begin, node_size_type const src_end,
    N       & target, node_size_type const tgt_begin
) noexcept
{
    BOOST_ASSUME( &source != &target ); // otherwise could require move_backwards or shift_*
    BOOST_ASSUME( src_begin <= src_end );
    BOOST_ASSUME( ( src_end - src_begin ) <= N::max_values );
    BOOST_ASSUME( tgt_begin < N::max_values );
    std::uninitialized_move( &key_at( source, src_begin ), &key_at( source, src_end ), &key_at( target, tgt_begin ) );
    if constexpr ( has_mapped_values<N> )
        std::uninitialized_move( &source.values[ source.start + src_begin ], &source.values[ source.start + src_end ], &target.values[ target.start + tgt_begin ] );
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
        child.parent_child_idx           = tgt_begin + ch_idx;
        child.mark_dirty();
    }
}

PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
