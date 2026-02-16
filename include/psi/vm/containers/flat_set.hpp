////////////////////////////////////////////////////////////////////////////////
/// psi::vm flat sorted set containers
///
/// Provides flat_set (unique keys) and flat_multiset (equivalent keys allowed).
///
/// Architecture:
///   flat_impl<Storage, Compare> — shared base holding storage, comparator,
///     capacity (empty, size, clear, reserve, shrink_to_fit), key_comp,
///     comparison, merge, lookup index helpers, and deducing-this sort utilities.
///   flat_set_impl<Key, Compare, KC> — set-specific base (inherits flat_impl).
///     Adds iterators, lookup, positional erase, erase by key,
///     extract/replace, observers, erase_if, capacity, span conversion.
///     Does NOT depend on uniqueness semantics.
///   flat_set<Key, Compare, KC> — unique sorted set (inherits flat_set_impl).
///     Adds unique emplace/insert, constructors with dedup, unique merge.
///   flat_multiset<Key, Compare, KC> — equivalent sorted set (inherits flat_set_impl).
///     Adds multi emplace/insert, constructors without dedup, multi merge.
///
/// Extensions beyond C++23 std::flat_set:
///   - reserve(n), shrink_to_fit(), capacity() — bulk pre-allocation / compaction
///   - merge(source) (lvalue & rvalue)         — std::set-style element transfer
///   - keys() / sequence()                     — const access to underlying container
///   - key_comp_mutable()                      — non-const comparator access
///
/// Copyright (c) Domagoj Saric.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
/// (See accompanying file LICENSE_1_0.txt or copy at
/// http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once

#include "flat_common.hpp"
#include "is_trivially_moveable.hpp"
#include "lookup.hpp"

#include <boost/assert.hpp>

#include <algorithm>
#include <compare>
#include <concepts>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// Forward declarations for friend access
template <typename Key, typename Compare, typename KeyContainer> class flat_set;
template <typename Key, typename Compare, typename KeyContainer> class flat_multiset;


//==============================================================================
// flat_set_impl — shared base for flat_set and flat_multiset
//
// Provides everything that does NOT depend on uniqueness semantics:
//   iterators, lookup, positional erase, erase by key,
//   extract/replace, observers, erase_if, capacity, span conversion.
//   Inherits from flat_impl for: storage, comparator, empty, size, clear,
//   reserve, shrink_to_fit, key_comp, comparison, merge, lookup helpers,
//   sort utilities.
//==============================================================================

template
<
    typename Key,
    typename Compare      = std::less<Key>,
    typename KeyContainer = std::vector<Key>
>
class flat_set_impl
    : public flat_impl<KeyContainer, Compare>
{
    using flat_base = flat_impl<KeyContainer, Compare>;

    static_assert( std::is_same_v<Key, typename KeyContainer::value_type>, "KeyContainer::value_type must be Key" );

    template <typename, typename, typename> friend class flat_set;
    template <typename, typename, typename> friend class flat_multiset;

public:
    //--------------------------------------------------------------------------
    // Member types (key_compare, nothrow_move_constructible — inherited)
    //--------------------------------------------------------------------------
    using typename flat_base::size_type;
    using typename flat_base::difference_type;
    using typename flat_base::key_type;
    using value_type         = Key;
    using value_compare      = Compare;
    using reference          = Key const &;
    using const_reference    = Key const &;
    using key_container_type = KeyContainer;

    // Inherited from flat_impl → Komparator
    using flat_base::transparent_comparator;
    using typename flat_base::key_const_arg;

    //--------------------------------------------------------------------------
    // Iterator — direct use of container's const_iterator (zero overhead,
    // matching MS STL's approach for flat_set)
    //--------------------------------------------------------------------------
public:
    using iterator               = typename KeyContainer::const_iterator;
    using const_iterator         = iterator; // set iterators are always const
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = reverse_iterator;

    //--------------------------------------------------------------------------
    // Iterators (cbegin/cend, rbegin/rend, crbegin/crend inherited from flat_impl)
    //--------------------------------------------------------------------------
    constexpr iterator begin() const noexcept { return this->storage_.cbegin(); }
    constexpr iterator end  () const noexcept { return this->storage_.cend();   }

    //--------------------------------------------------------------------------
    // Capacity (empty, size, reserve, shrink_to_fit — inherited from flat_impl)
    //--------------------------------------------------------------------------
    [[nodiscard]] constexpr size_type max_size() const noexcept { return this->storage_.max_size(); }

    //--------------------------------------------------------------------------
    // Lookup — find, contains, count, lower_bound, upper_bound, equal_range
    // are all inherited from flat_impl.
    //--------------------------------------------------------------------------

    //--------------------------------------------------------------------------
    // Modifiers — erase
    //--------------------------------------------------------------------------
    constexpr iterator erase( const_iterator pos ) noexcept {
        return this->erase_pos_impl( pos );
    }
    constexpr iterator erase( const_iterator first, const_iterator last ) noexcept {
        return this->erase_range_impl( first, last );
    }
    template <LookupType<transparent_comparator, key_type> K = key_type>
    constexpr size_type erase( K const & key ) noexcept {
        return this->erase_by_key_impl( key );
    }

    //--------------------------------------------------------------------------
    // Extraction & replacement (extract, keys inherited from flat_impl)
    //--------------------------------------------------------------------------
    constexpr void replace( KeyContainer keys ) noexcept( std::is_nothrow_move_assignable_v<KeyContainer> ) {
        this->storage_ = std::move( keys );
    }

    //--------------------------------------------------------------------------
    // Observers (key_comp, key_comp_mutable — inherited from flat_impl)
    //--------------------------------------------------------------------------
    [[nodiscard]] constexpr value_compare value_comp() const noexcept { return this->key_comp(); }

    // Boost compat alias (keys() inherited from flat_impl)
    [[nodiscard]] constexpr key_container_type const & sequence() const noexcept { return this->storage_; }

    // erase_if: inherited from flat_impl (friend, found via ADL)

    //--------------------------------------------------------------------------
    // Extensions — capacity (reserve, shrink_to_fit — inherited from flat_impl)
    //--------------------------------------------------------------------------
    [[nodiscard]] constexpr size_type capacity() const noexcept
    requires requires( KeyContainer const & kc ) { kc.capacity(); }
    {
        return static_cast<size_type>( this->storage_.capacity() );
    }

    //--------------------------------------------------------------------------
    // Span conversion (implicit)
    //--------------------------------------------------------------------------
    constexpr operator std::span<Key const>() const noexcept
    requires std::ranges::contiguous_range<KeyContainer const>
    {
        return std::span<Key const>{ this->storage_.data(), this->storage_.size() };
    }

    //--------------------------------------------------------------------------
    // Container extraction / adoption (boost flat_set compat)
    //--------------------------------------------------------------------------
    constexpr key_container_type extract_sequence() noexcept {
        return std::move( this->storage_ );
    }

    // Adopt a pre-sorted (and, for unique sets, deduplicated) container
    constexpr void adopt_sequence( sorted_unique_t, key_container_type keys ) noexcept {
        this->storage_ = std::move( keys );
    }

    // Adopt an unsorted container — sorts (and dedups for unique sets)
    constexpr void adopt_sequence( key_container_type keys ) {
        this->storage_ = std::move( keys );
        this->init_sort();
    }

    // Key-container iterator → set iterator (identity: key iterator IS the set iterator)
    static constexpr iterator iter_from_key( auto const key_it ) noexcept { return key_it; }

    // Iterator factory from index position
    constexpr iterator make_iter( size_type const pos ) const noexcept {
        return this->storage_.cbegin() + static_cast<difference_type>( pos );
    }

    // Iterator → index conversion
    constexpr size_type iter_index( const_iterator const it ) const noexcept {
        return static_cast<size_type>( it - this->storage_.cbegin() );
    }

    //--------------------------------------------------------------------------
    // Protected interface for derived classes
    //--------------------------------------------------------------------------
protected:
    constexpr flat_set_impl() = default;

    constexpr explicit flat_set_impl( Compare const & comp ) noexcept( std::is_nothrow_copy_constructible_v<Compare> )
        : flat_base{ comp } {}

    constexpr flat_set_impl( Compare const & comp, KeyContainer storage ) noexcept( flat_base::nothrow_move_constructible )
        : flat_base{ comp, std::move( storage ) } {}

    // "Fake deducing-this" forwarder — passes Derived & through to flat_impl
    template <typename Derived, typename... Args>
    requires std::derived_from<std::remove_cvref_t<Derived>, flat_set_impl>
    constexpr flat_set_impl( Derived & self, Args &&... args )
        : flat_base{ self, std::forward<Args>( args )... } {}

    // Pre-sorted iterator pair (no Derived needed — no sorting)
    template <std::input_iterator InputIt>
    constexpr flat_set_impl( Compare const & comp, InputIt first, InputIt last )
        : flat_base{ comp, first, last } {}

    constexpr flat_set_impl( flat_set_impl const & ) = default;
    constexpr flat_set_impl( flat_set_impl && )      = default;
    constexpr flat_set_impl & operator=( flat_set_impl const & ) = default;
    constexpr flat_set_impl & operator=( flat_set_impl && )      = default;
}; // class flat_set_impl


//==============================================================================
// flat_set — unique sorted set
//==============================================================================

template
<
    typename Key,
    typename Compare      = std::less<Key>,
    typename KeyContainer = std::vector<Key>
>
class flat_set
    : public flat_set_impl<Key, Compare, KeyContainer>
{
    using base = flat_set_impl<Key, Compare, KeyContainer>;
    using typename base::difference_type;

public:
    using typename base::key_type;
    using typename base::value_type;
    using typename base::size_type;
    using typename base::iterator;
    using typename base::const_iterator;
    using sorted_hint_t = sorted_unique_t;
    static constexpr bool unique{ true };
    using base::transparent_comparator;

    //--------------------------------------------------------------------------
    // Constructors — one-liners via "fake deducing-this" base constructors
    //--------------------------------------------------------------------------
    constexpr flat_set() = default;

    constexpr explicit flat_set( Compare const & comp ) noexcept( std::is_nothrow_copy_constructible_v<Compare> )
        : base{ comp } {}

    constexpr explicit flat_set( KeyContainer keys, Compare const & comp = Compare{} )
        : base{ *this, comp, std::move( keys ) } {}

    constexpr flat_set( sorted_unique_t, KeyContainer keys, Compare const & comp = Compare{} ) noexcept( base::nothrow_move_constructible )
        : base{ comp, std::move( keys ) } {}

    template <std::input_iterator InputIt>
    constexpr flat_set( InputIt first, InputIt const last, Compare const & comp = Compare{} )
        : base{ *this, comp, first, last } {}

    template <std::input_iterator InputIt>
    constexpr flat_set( sorted_unique_t, InputIt first, InputIt const last, Compare const & comp = Compare{} )
        : base{ comp, first, last } {}

    template <std::ranges::input_range R>
    constexpr flat_set( std::from_range_t tag, R && rg, Compare const & comp = Compare{} )
        : base{ *this, comp, tag, std::forward<R>( rg ) } {}

    constexpr flat_set( std::initializer_list<value_type> const il, Compare const & comp = Compare{} )
        : flat_set( il.begin(), il.end(), comp ) {}

    constexpr flat_set( sorted_unique_t s, std::initializer_list<value_type> il, Compare const & comp = Compare{} )
        : flat_set( s, il.begin(), il.end(), comp ) {}

    constexpr flat_set( flat_set const & ) = default;
    constexpr flat_set( flat_set && )      = default;

    constexpr flat_set & operator=( flat_set const & ) = default;
    constexpr flat_set & operator=( flat_set && )      = default;

    constexpr flat_set & operator=( std::initializer_list<value_type> il ) {
        this->assign( il );
        return *this;
    }

    //--------------------------------------------------------------------------
    // Swap (type-safe: only flat_set ↔ flat_set, not flat_set ↔ flat_multiset)
    //--------------------------------------------------------------------------
    constexpr void swap( flat_set & other ) noexcept { this->swap_impl( other ); }
    friend constexpr void swap( flat_set & a, flat_set & b ) noexcept { a.swap( b ); }

    //--------------------------------------------------------------------------
    // Lookup — find, contains, count, lower_bound, upper_bound, equal_range
    // inherited from flat_impl. count() uses self.unique for optimization.
    //--------------------------------------------------------------------------

    //--------------------------------------------------------------------------
    // Modifiers — optimized erase by key for unique keys (single binary search)
    //--------------------------------------------------------------------------
#ifndef _MSC_VER
    using base::erase; // positional + range + by-key from flat_set_impl
#else
    // MSVC workaround: using-declaration fails to hide the base erase(K const &)
    // template when the derived class provides its own override, causing C2668
    // (ambiguous overloaded call). Per [namespace.udecl]/15, the local declaration
    // should hide the one introduced by the using-declaration.
    constexpr iterator erase( const_iterator pos ) noexcept { return base::erase( pos ); }
    constexpr iterator erase( const_iterator first, const_iterator last ) noexcept { return base::erase( first, last ); }
#endif

    template <LookupType<transparent_comparator, key_type> K = key_type>
    constexpr size_type erase( K const & key ) noexcept {
        auto const it{ this->find( key ) };
        if ( it == this->end() ) return 0;
        detail::storage_erase_at( this->storage_, this->iter_index( it ) );
        return 1;
    }

    //--------------------------------------------------------------------------
    // Modifiers — unique insert / emplace
    //--------------------------------------------------------------------------
    using base::insert;       // bulk insert, initializer_list, sorted bulk — from flat_impl
    using base::insert_range; // insert_range, sorted insert_range — from flat_impl

    constexpr auto insert( value_type const & v ) { return emplace( v ); }
    constexpr auto insert( value_type &&      v ) { return emplace( std::move( v ) ); }

    constexpr iterator insert( const_iterator hint, value_type const & v ) { return emplace_hint( hint, v ); }
    constexpr iterator insert( const_iterator hint, value_type &&      v ) { return emplace_hint( hint, std::move( v ) ); }

    // Boost compat: insert range known to contain unique (but unsorted) elements.
    // Caller guarantees no duplicates within [first, last); dedup still runs
    // against existing container elements. Currently delegates to insert() —
    // the name documents the caller's guarantee.
    template <std::input_iterator InputIt>
    constexpr void insert_unique( InputIt first, InputIt last ) {
        this->template bulk_insert<false>( first, last );
    }

    template <typename... Args>
    constexpr std::pair<iterator, bool> emplace( Args &&... args ) {
        value_type v( std::forward<Args>( args )... );
        auto const pos{ this->lower_bound_index( v ) };
        if ( this->key_eq_at( pos, v ) )
            return { this->make_iter( pos ), false };
        this->storage_.insert( this->storage_.begin() + static_cast<difference_type>( pos ), std::move( v ) );
        return { this->make_iter( pos ), true };
    }

    constexpr iterator emplace_hint( const_iterator hint, auto &&... args ) {
        value_type v( std::forward<decltype( args )>( args )... );
        auto const hintIdx{ this->iter_index( hint ) };
        bool const hintValid{
            ( hintIdx == 0             || this->le( this->storage_[ hintIdx - 1 ], v ) ) &&
            ( hintIdx >= this->size()  || this->le( v, this->storage_[ hintIdx ] ) )
        };
        if ( hintValid ) {
            this->storage_.insert( this->storage_.begin() + static_cast<difference_type>( hintIdx ), std::move( v ) );
            return this->make_iter( hintIdx );
        }
        if ( hintIdx < this->size() && this->eq( v, this->storage_[ hintIdx ] ) )
            return this->make_iter( hintIdx );
        return emplace( std::move( v ) ).first;
    }
}; // class flat_set


//==============================================================================
// flat_multiset — sorted set with equivalent keys allowed
//==============================================================================

template
<
    typename Key,
    typename Compare      = std::less<Key>,
    typename KeyContainer = std::vector<Key>
>
class flat_multiset
    : public flat_set_impl<Key, Compare, KeyContainer>
{
    using base = flat_set_impl<Key, Compare, KeyContainer>;
    using typename base::difference_type;

public:
    using typename base::key_type;
    using typename base::value_type;
    using typename base::size_type;
    using typename base::iterator;
    using typename base::const_iterator;
    using sorted_hint_t = sorted_equivalent_t;
    static constexpr bool unique{ false };

    //--------------------------------------------------------------------------
    // Constructors — one-liners via "fake deducing-this" base constructors
    //--------------------------------------------------------------------------
    constexpr flat_multiset() = default;

    constexpr explicit flat_multiset( Compare const & comp ) noexcept( std::is_nothrow_copy_constructible_v<Compare> )
        : base{ comp } {}

    constexpr explicit flat_multiset( KeyContainer keys, Compare const & comp = Compare{} )
        : base{ *this, comp, std::move( keys ) } {}

    constexpr flat_multiset( sorted_equivalent_t, KeyContainer keys, Compare const & comp = Compare{} ) noexcept( base::nothrow_move_constructible )
        : base{ comp, std::move( keys ) } {}

    template <std::input_iterator InputIt>
    constexpr flat_multiset( InputIt first, InputIt const last, Compare const & comp = Compare{} )
        : base{ *this, comp, first, last } {}

    template <std::input_iterator InputIt>
    constexpr flat_multiset( sorted_equivalent_t, InputIt first, InputIt const last, Compare const & comp = Compare{} )
        : base{ comp, first, last } {}

    template <std::ranges::input_range R>
    constexpr flat_multiset( std::from_range_t tag, R && rg, Compare const & comp = Compare{} )
        : base{ *this, comp, tag, std::forward<R>( rg ) } {}

    constexpr flat_multiset( std::initializer_list<value_type> const il, Compare const & comp = Compare{} )
        : flat_multiset( il.begin(), il.end(), comp ) {}

    constexpr flat_multiset( sorted_equivalent_t s, std::initializer_list<value_type> il, Compare const & comp = Compare{} )
        : flat_multiset( s, il.begin(), il.end(), comp ) {}

    constexpr flat_multiset( flat_multiset const & ) = default;
    constexpr flat_multiset( flat_multiset && )      = default;

    constexpr flat_multiset & operator=( flat_multiset const & ) = default;
    constexpr flat_multiset & operator=( flat_multiset && )      = default;

    constexpr flat_multiset & operator=( std::initializer_list<value_type> il ) {
        this->assign( il );
        return *this;
    }

    //--------------------------------------------------------------------------
    // Swap (type-safe)
    //--------------------------------------------------------------------------
    constexpr void swap( flat_multiset & other ) noexcept { this->swap_impl( other ); }
    friend constexpr void swap( flat_multiset & a, flat_multiset & b ) noexcept { a.swap( b ); }

    //--------------------------------------------------------------------------
    // Modifiers — multi insert / emplace
    //--------------------------------------------------------------------------
    using base::insert;       // bulk insert, initializer_list, sorted bulk — from flat_impl
    using base::insert_range; // insert_range, sorted insert_range — from flat_impl

    constexpr auto insert( value_type const & v ) { return emplace( v ); }
    constexpr auto insert( value_type &&      v ) { return emplace( std::move( v ) ); }

    constexpr iterator insert( const_iterator hint, value_type const & v ) { return emplace_hint( hint, v ); }
    constexpr iterator insert( const_iterator hint, value_type &&      v ) { return emplace_hint( hint, std::move( v ) ); }

    template <typename... Args>
    constexpr iterator emplace( Args &&... args ) {
        value_type v( std::forward<Args>( args )... );
        auto const pos{ this->lower_bound_index( v ) };
        this->storage_.insert( this->storage_.begin() + static_cast<difference_type>( pos ), std::move( v ) );
        return this->make_iter( pos );
    }

    constexpr iterator emplace_hint( const_iterator hint, auto &&... args ) {
        value_type v( std::forward<decltype( args )>( args )... );
        auto const pos{ this->hinted_insert_pos( this->iter_index( hint ), enreg( v ) ) };
        this->storage_.insert( this->storage_.begin() + static_cast<difference_type>( pos ), std::move( v ) );
        return this->make_iter( pos );
    }

}; // class flat_multiset


//------------------------------------------------------------------------------
// Deduction guides — flat_set
//------------------------------------------------------------------------------

// Container (unsorted)
template <typename KC, typename Comp = std::less<typename KC::value_type>>
requires( !std::is_same_v<KC, sorted_unique_t> && !std::is_same_v<KC, sorted_equivalent_t> )
flat_set( KC, Comp = Comp{} )
    -> flat_set<typename KC::value_type, Comp, KC>;

// Container (sorted unique)
template <typename KC, typename Comp = std::less<typename KC::value_type>>
flat_set( sorted_unique_t, KC, Comp = Comp{} )
    -> flat_set<typename KC::value_type, Comp, KC>;

// Iterator range (unsorted)
template <std::input_iterator InputIt, typename Comp = std::less<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type>>>
requires( !std::is_same_v<InputIt, sorted_unique_t> && !std::is_same_v<InputIt, sorted_equivalent_t> )
flat_set( InputIt, InputIt, Comp = Comp{} )
    -> flat_set<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type>, Comp>;

// Iterator range (sorted unique)
template <std::input_iterator InputIt, typename Comp = std::less<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type>>>
flat_set( sorted_unique_t, InputIt, InputIt, Comp = Comp{} )
    -> flat_set<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type>, Comp>;

// from_range_t
template <std::ranges::input_range R, typename Comp = std::less<std::remove_const_t<std::ranges::range_value_t<R>>>>
flat_set( std::from_range_t, R &&, Comp = Comp{} )
    -> flat_set<std::remove_const_t<std::ranges::range_value_t<R>>, Comp>;

// Initializer list (unsorted)
template <typename Key, typename Comp = std::less<Key>>
flat_set( std::initializer_list<Key>, Comp = Comp{} )
    -> flat_set<Key, Comp>;

// Initializer list (sorted unique)
template <typename Key, typename Comp = std::less<Key>>
flat_set( sorted_unique_t, std::initializer_list<Key>, Comp = Comp{} )
    -> flat_set<Key, Comp>;


//------------------------------------------------------------------------------
// Deduction guides — flat_multiset
//------------------------------------------------------------------------------

// Container (unsorted)
template <typename KC, typename Comp = std::less<typename KC::value_type>>
requires( !std::is_same_v<KC, sorted_unique_t> && !std::is_same_v<KC, sorted_equivalent_t> )
flat_multiset( KC, Comp = Comp{} )
    -> flat_multiset<typename KC::value_type, Comp, KC>;

// Container (sorted equivalent)
template <typename KC, typename Comp = std::less<typename KC::value_type>>
flat_multiset( sorted_equivalent_t, KC, Comp = Comp{} )
    -> flat_multiset<typename KC::value_type, Comp, KC>;

// Iterator range (unsorted)
template <std::input_iterator InputIt, typename Comp = std::less<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type>>>
requires( !std::is_same_v<InputIt, sorted_unique_t> && !std::is_same_v<InputIt, sorted_equivalent_t> )
flat_multiset( InputIt, InputIt, Comp = Comp{} )
    -> flat_multiset<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type>, Comp>;

// Iterator range (sorted equivalent)
template <std::input_iterator InputIt, typename Comp = std::less<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type>>>
flat_multiset( sorted_equivalent_t, InputIt, InputIt, Comp = Comp{} )
    -> flat_multiset<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type>, Comp>;

// from_range_t
template <std::ranges::input_range R, typename Comp = std::less<std::remove_const_t<std::ranges::range_value_t<R>>>>
flat_multiset( std::from_range_t, R &&, Comp = Comp{} )
    -> flat_multiset<std::remove_const_t<std::ranges::range_value_t<R>>, Comp>;

// Initializer list (unsorted)
template <typename Key, typename Comp = std::less<Key>>
flat_multiset( std::initializer_list<Key>, Comp = Comp{} )
    -> flat_multiset<Key, Comp>;

// Initializer list (sorted equivalent)
template <typename Key, typename Comp = std::less<Key>>
flat_multiset( sorted_equivalent_t, std::initializer_list<Key>, Comp = Comp{} )
    -> flat_multiset<Key, Comp>;


//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------

template <typename Key, typename Compare, typename KC>
bool constexpr psi::vm::is_trivially_moveable<psi::vm::flat_set<Key, Compare, KC>>{ psi::vm::is_trivially_moveable<Compare> };

template <typename Key, typename Compare, typename KC>
bool constexpr psi::vm::is_trivially_moveable<psi::vm::flat_multiset<Key, Compare, KC>>{ psi::vm::is_trivially_moveable<Compare> };
//------------------------------------------------------------------------------
