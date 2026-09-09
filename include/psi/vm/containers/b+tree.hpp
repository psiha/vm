#pragma once
////////////////////////////////////////////////////////////////////////////////
///
/// ile b+tree.hpp
///
/// The public b+tree interface. The implementation it rests on is split by
/// what each layer needs to know - see the b+tree/ directory.
///
/// Copyright (c) Domagoj Saric.
///
////////////////////////////////////////////////////////////////////////////////

#include "b+tree/impl.hpp"

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
    template <comparator_erasure Erasure = Komparator<Comparator>::erasure, std::input_iterator InIter>
    size_type insert( InIter const begin, InIter const end ) { return impl_base::template insert<Erasure>( this->bulk_insert_prepare( std::ranges::subrange( begin, end ) ), unique ); }
    template <comparator_erasure Erasure = Komparator<Comparator>::erasure, std::convertible_to<Key> T>
    size_type insert( std::initializer_list<T> const  keys ) { return impl_base::template insert<Erasure>( this->bulk_insert_prepare( std::ranges::subrange( keys       ) ), unique ); }
    template <comparator_erasure Erasure = Komparator<Comparator>::erasure>
    size_type insert( std::ranges::range auto const & keys ) { return impl_base::template insert<Erasure>( this->bulk_insert_prepare( std::ranges::subrange( keys       ) ), unique ); }

    size_type insert_presorted       ( std::span<Key const> const presorted_input ) { return impl_base::insert_presorted       ( presorted_input, unique ); }
    size_type insert_presorted_unique( std::span<Key const> const presorted_input ) { return impl_base::insert_presorted_unique( presorted_input, unique ); }

    size_type merge( bp_tree       && other ) { return impl_base::merge( std::move( other ), unique ); }
    size_type merge( bp_tree const &  other ) { return impl_base::merge(            other  , unique ); }

    [[ gnu::sysv_abi, gnu::noinline ]]
    bool erase( key_const_arg key ) noexcept
    requires( unique )
    {
        if ( empty() )
            return 0;

        auto const location{ this->find_nodes_for( key, unique ) };
        if ( !location.leaf_offset.exact_find ) [[ unlikely ]]
            return false;

        leaf_node & leaf{ location.leaf };
        if ( this->hdr().depth_ != 1 ) // i.e. leaf is not the root
            this->verify_min_max( leaf );

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

        leaf_node & leaf{ location.leaf };
        auto const leaf_key_offset{ location.leaf_offset.pos };
        // Complex check to see if there is only one key to erase, i.e. expect
        // nonunique keys to be an unlikely occurrence.
        // TODO measure if this is worth it.
        if
        (
            (
                ( ( leaf_key_offset + 1 ) < leaf.num_vals ) &&
                lt( key, leaf.keys[ leaf_key_offset + 1 ] )
            ) ||
            ( !leaf.right ) ||
            lt( key, this->right( leaf ).keys[ 0 ] )
        ) [[ likely ]]
        {
            return this->erase_single( location );
        }

        // try and efficiently handle multiple erased values
        auto p_node{ &leaf };
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
                this->remove_from_parent  ( node );
                this->unlink_and_free_node( node, this->left( node ) );
            }
            else
            {
                shift_entries_left( node, node_offset, node.num_vals, erased_count );
                node.num_vals -= erased_count;
                node.mark_dirty();
                if ( node_offset == 0 ) {
                    // erasure from the beginning of the node - implies not till
                    // the end of the node (as this, entire node erasure case,
                    // is handled first/above) - this means we've reached the
                    // end of the erasure loop (i.e. no more keys to erase)
                    BOOST_ASSUME( end_pos < node.num_vals + erased_count );
                    this->update_separator                     ( node );
                    this->check_and_handle_bulk_erase_underflow( node );
                    break;
                } else {
                    BOOST_ASSUME( end_pos == node.num_vals + erased_count );
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
        if ( leaf.num_vals ) // first check for deletion of the starting node
            this->check_and_handle_bulk_erase_underflow( leaf );

        this->hdr().size_ -= count;
        return count;
    }

    size_type replace_keys_inplace( std::span<Key const> const old_keys, std::span<Key const> const new_keys ) noexcept { return impl_base::replace_keys_inplace( old_keys, new_keys, unique ); }
    size_type erase_sorted        ( std::span<Key const> const keys_to_remove ) noexcept { return impl_base::erase_sorted( keys_to_remove, unique ); }
    size_type erase_sorted_exact  ( std::span<Key const> const keys_to_remove ) noexcept { return impl_base::erase_sorted_exact( keys_to_remove, unique ); }

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
