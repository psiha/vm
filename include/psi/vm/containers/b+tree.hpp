#pragma once
////////////////////////////////////////////////////////////////////////////////
///
/// \file b+tree.hpp
///
/// The public b+tree interface. The implementation it rests on is split by
/// what each layer needs to know - see the b+tree/ directory.
///
/// Copyright (c) Domagoj Saric.
///
////////////////////////////////////////////////////////////////////////////////

#include "b+tree/impl.hpp"

#include <psi/vm/containers/komparator.hpp>
#include <psi/vm/containers/lookup.hpp>

#include <psi/build/disable_warnings.hpp>

#include <boost/assert.hpp>

#include <algorithm>
#include <functional>
#include <iterator>
#include <ranges>
#include <span>
#include <utility>

//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

PSI_WARNING_DISABLE_PUSH()
PSI_WARNING_MSVC_DISABLE( 4127 ) // conditional expression is constant
PSI_WARNING_MSVC_DISABLE( 5030 ) // unrecognized attribute

template <typename Key, bool unique, typename Comparator = std::less<>>
class bp_tree
    :
    public bp_tree_impl<Key, Comparator>
{
private:
    using impl_base = bp_tree_impl<Key, Comparator>;

    using impl_base::leaf;
    using impl_base::make_iter;

public:
    using impl_base::impl_base; // inherit constructors (default, Comparator, COW)

    static constexpr auto transparent_comparator{ impl_base::transparent_comparator };
    // non-contiguous insert_presorted* input is gathered and inserted in chunks of this many keys
    static constexpr auto presorted_range_chunk_size{ impl_base::presorted_range_chunk_size };

    using const_iterator  = impl_base::const_iterator;
    using const_iter_pair = impl_base::const_iter_pair;
    using size_type       = impl_base::size_type;
    using node_size_type  = impl_base::node_size_type;
    using key_const_arg   = impl_base::key_const_arg;
    using leaf_node       = impl_base::leaf_node;
    using root_node       = impl_base::root_node;
    using inner_node      = impl_base::inner_node;
    using parent_node     = impl_base::parent_node;

    using impl_base::empty;
    using impl_base::end;
    using impl_base::erase;
    using impl_base::eq;
    using impl_base::lt;

    [[ nodiscard ]] const_iterator find       ( LookupType<transparent_comparator, Key> auto const & key ) const noexcept { return impl_base::find_impl       ( pass_in_reg{ key }, unique ); }
    [[ nodiscard ]] const_iterator lower_bound( LookupType<transparent_comparator, Key> auto const & key ) const noexcept { return impl_base::lower_bound_impl( pass_in_reg{ key }, unique ); }
    [[ nodiscard ]] auto           equal_range( LookupType<transparent_comparator, Key> auto const & key ) const noexcept { return            equal_range_impl( pass_in_reg{ key } ); }
    [[ nodiscard ]] bool           contains   ( LookupType<transparent_comparator, Key> auto const & key ) const noexcept { return impl_base::contains_impl   ( pass_in_reg{ key }, unique ); }
    // Forward-only lower_bound: the first element >= key at or after pos (key
    // must not be less than the element at pos) - in a non-unique tree the
    // first of a run of equivalent elements that starts after pos. Returns
    // end() only when key > all elements from pos on.
    // The Search parameter selects, PER CALL, how the rest of the leaf at pos
    // is searched (see forward_search; the same holds for every forward
    // search below: replace_keys_inplace, erase_sorted*, and the insertion
    // point search of the bulk inserts and merge).
    template <forward_search Search = forward_search::automatic, LookupType<transparent_comparator, Key> K>
    [[ nodiscard ]] const_iterator lower_bound_from( const_iterator const pos, K const & key ) const noexcept { return impl_base::template lower_bound_from_impl<Search>( pos.base().pos(), pass_in_reg{ key }, unique ); }

    const_iterator insert( const_iterator const pos_hint, InsertableType<transparent_comparator, Key> auto const & key ) { return impl_base::insert_impl( pos_hint, pass_in_reg{ key }, unique ); }
    auto           insert(                                InsertableType<transparent_comparator, Key> auto const & key )
    {
        auto const result{ impl_base::insert_impl( pass_in_reg{ key }, unique ) };
        if constexpr ( unique ) {
            return result;
        } else {
            BOOST_ASSUME( result.second );
            return result.first;
        }
    }

    // bulk insert
    // performance note: insertion of existing values into a unique bp_tree_impl
    // is supported and accounted for (the input values are skipped) but it is
    // considered an 'unlikely' event and as such it is handled by sad/cold paths
    // TODO complete std insert interface (w/ ranges, iterators, hints...)
    // The Erasure parameter selects the type-erased bulk-sort codegen PER
    // CALL (default: the comparator-derived policy) — the bulk-load sort is
    // where the per-comparator instantiation bloat lives; lookups, iteration
    // and the container type itself stay unaffected/shared.
    // The Search parameter: see lower_bound_from.
    template <comparator_erasure Erasure = Komparator<Comparator>::erasure, forward_search Search = forward_search::automatic, std::input_iterator InIter>
    size_type insert( InIter const first, InIter const last ) { return impl_base::template insert<Erasure, Search>( this->bulk_insert_prepare( std::ranges::subrange( first, last ) ), unique ); }
    template <comparator_erasure Erasure = Komparator<Comparator>::erasure, forward_search Search = forward_search::automatic, std::convertible_to<Key> T>
    size_type insert( std::initializer_list<T> const  keys ) { return impl_base::template insert<Erasure, Search>( this->bulk_insert_prepare( std::ranges::subrange( keys       ) ), unique ); }
    template <comparator_erasure Erasure = Komparator<Comparator>::erasure, forward_search Search = forward_search::automatic>
    size_type insert( std::ranges::range auto const & keys ) { return impl_base::template insert<Erasure, Search>( this->bulk_insert_prepare( std::ranges::subrange( keys       ) ), unique ); }

    template <forward_search Search = forward_search::automatic>
    size_type insert_presorted       ( std::span<Key const> const presorted_input ) { return impl_base::template insert_presorted       <Search>( presorted_input, unique ); }
    template <forward_search Search = forward_search::automatic>
    size_type insert_presorted_unique( std::span<Key const> const presorted_input ) { return impl_base::template insert_presorted_unique<Search>( presorted_input, unique ); }
    // Any sorted input range (unique: also duplicate-free) - e.g. a merge of
    // sorted sequences or a view - inserted without materialising it first.
    template <forward_search Search = forward_search::automatic, std::ranges::input_range R> requires std::convertible_to<std::ranges::range_reference_t<R>, Key>
    size_type insert_presorted       ( R && presorted_input ) { return impl_base::template insert_presorted_range<true , Search>( std::forward<R>( presorted_input ), unique ); }
    template <forward_search Search = forward_search::automatic, std::ranges::input_range R> requires std::convertible_to<std::ranges::range_reference_t<R>, Key>
    size_type insert_presorted_unique( R && presorted_input ) { return impl_base::template insert_presorted_range<false, Search>( std::forward<R>( presorted_input ), unique ); }

    template <forward_search Search = forward_search::automatic>
    size_type merge( bp_tree       && other ) { return impl_base::template merge<Search>( std::move( other ), unique ); }
    template <forward_search Search = forward_search::automatic>
    size_type merge( bp_tree const &  other ) { return impl_base::template merge<Search>(            other  , unique ); }

    [[ gnu::sysv_abi, gnu::noinline ]]
    bool erase( key_const_arg key ) noexcept
    requires( unique )
    {
        if ( empty() )
            return 0;

        auto const location{ this->find_nodes_for( key, unique ) };
        if ( !location.leaf_offset.exact_find ) [[ unlikely ]]
            return false;

        leaf_node & found_leaf{ location.leaf };
        if ( this->hdr().depth_ != 1 ) // i.e. leaf is not the root
            this->verify_min_max( found_leaf );

        return this->erase_single( location );
    }

    [[ nodiscard ]] [[ gnu::sysv_abi, gnu::noinline ]]
    size_type erase( key_const_arg key ) noexcept
    requires( !unique )
    {
        if ( empty() )
            return 0;

        auto const location{ this->find_nodes_for( key, unique ) };
        if ( !location.leaf_offset.exact_find ) [[ unlikely ]]
            return 0;

        leaf_node & found_leaf{ location.leaf };
        auto const leaf_key_offset{ location.leaf_offset.pos };
        // Complex check to see if there is only one key to erase, i.e. expect
        // nonunique keys to be an unlikely occurrence: the key that follows -
        // in this leaf or, past its end, at the start of the next one - has to
        // be greater.
        // TODO measure if this is worth it.
        if
        (
            ( ( leaf_key_offset + 1 ) < found_leaf.num_vals )
                ? lt( key, found_leaf.key( leaf_key_offset + 1 ) )
                : ( !found_leaf.right || lt( key, this->right( found_leaf ).key( 0 ) ) )
        ) [[ likely ]]
        {
            return this->erase_single( location );
        }

        // try and efficiently handle multiple erased values: leaf by leaf, a
        // whole leaf at a time where the run covers it
        auto p_node{ &found_leaf };
        auto node_offset{ leaf_key_offset };
        size_type count{ 0 };
        for ( ; ; )
        {
            auto & node{ *p_node };
            auto const end_pos{ this->upper_bound( node, node_offset, key ) };
            auto const erased_count{ static_cast<node_size_type>( end_pos - node_offset ) };
            count += erased_count;

            auto const next_node{ node.right };
            if ( erased_count == node.num_vals ) // entire node erased
            {
                if ( node.is_root() ) [[ unlikely ]] // the run was the whole tree
                {
                    this->free_root_leaf( node );
                    break;
                }
                // (also the leftmost leaf - whose right sibling then becomes
                // the leftmost)
                this->remove_from_parent  ( node );
                this->unlink_and_free_leaf( node );
            }
            else
            {
                bptree_base::shift_entries_left( node, node_offset, node.num_vals, erased_count );
                node.num_vals -= erased_count;
                this->mark_dirty( node );
                if ( node_offset == 0 ) {
                    // erasure from the beginning of the node - implies not till
                    // the end of the node (as this, entire node erasure case,
                    // is handled first/above) - this means we've reached the
                    // end of the erasure loop (i.e. no more keys to erase)
                    BOOST_ASSUME( end_pos < node.num_vals + erased_count );
                    this->update_separator                     ( node );
                    this->check_and_handle_bulk_erase_underflow( node );
                    break;
                } else if ( end_pos < node.num_vals + erased_count ) {
                    // the run ended within the starting node (whose underflow
                    // is handled below)
                    break;
                }
            }
            if ( !next_node )
                break;
            p_node      = &this->leaf( next_node );
            node_offset = 0;
        }

        // handling of possible underflow of the starting node is delayed to
        // avoid constant moving/refilling of values from succeeding right
        // leaves - rather this case is handled faster by removing entire
        // same-valued nodes (in case there are any) and then the starting and
        // ending, potentially partially erased, leaves are handled for possible
        // underflow
        if ( found_leaf.num_vals ) // first check for deletion of the starting node
            this->check_and_handle_bulk_erase_underflow( found_leaf );

        this->hdr().size_ -= count;
        return count;
    }

    template <forward_search Search = forward_search::automatic>
    size_type replace_keys_inplace( std::span<Key const> const old_keys, std::span<Key const> const new_keys ) noexcept { return impl_base::template replace_keys_inplace<Search>( old_keys, new_keys, unique ); }
    template <forward_search Search = forward_search::automatic>
    size_type erase_sorted        ( std::span<Key const> const keys_to_remove ) noexcept { return impl_base::template erase_sorted      <Search>( keys_to_remove, unique ); }
    template <forward_search Search = forward_search::automatic>
    size_type erase_sorted_exact  ( std::span<Key const> const keys_to_remove ) noexcept { return impl_base::template erase_sorted_exact<Search>( keys_to_remove, unique ); }

private:
    [[ using gnu: pure, sysv_abi ]]
    auto equal_range_impl( Reg auto const key ) const noexcept
    {
        auto const [p_leaf, leaf_offset]{ this->find_internal( key, unique ) };
        if ( p_leaf ) [[ likely ]] {
            auto const begin{ make_iter( *p_leaf, leaf_offset ) };
            if constexpr ( unique ) {
                return std::ranges::subrange( begin, std::next( begin ), 1 );
            } else {
                auto const [pos, count]{ this->upper_bound_across_nodes( *p_leaf, leaf_offset, key ) };
                return std::ranges::subrange
                (
                    begin,
                    make_iter( pos ),
                    count
                );
            }
        }

        auto const end_iter{ end() };
        return std::ranges::subrange( end_iter, end_iter, 0 );
    }
}; // class bp_tree

template <typename Key, typename Comparator = std::less<>> using bptree_set      = bp_tree<Key, true , Comparator>;
template <typename Key, typename Comparator = std::less<>> using bptree_multiset = bp_tree<Key, false, Comparator>;


PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
