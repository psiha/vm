////////////////////////////////////////////////////////////////////////////////
/// psi::vm flat sorted associative map containers
///
/// Provides flat_map (unique keys) and flat_multimap (equivalent keys allowed).
///
/// Architecture:
///   flat_impl<Storage, Compare> — shared base holding storage, comparator,
///     capacity (empty, size, clear, reserve, shrink_to_fit), key_comp,
///     comparison, merge, lookup index helpers, and deducing-this sort utilities.
///   flat_map_impl<Key, T, Compare, KC, MC> — map-specific base (inherits flat_impl).
///     Adds iterator, lookup, positional erase, erase by key,
///     extract/replace, observers, erase_if.
///     Does NOT depend on uniqueness semantics.
///   flat_map<Key, T, Compare, KC, MC> — unique sorted map (inherits flat_map_impl).
///     Adds unique emplace/insert, operator[], at(), try_emplace, insert_or_assign,
///     constructors with dedup, unique merge.
///   flat_multimap<Key, T, Compare, KC, MC> — equivalent sorted map (inherits flat_map_impl).
///     Adds multi emplace/insert, constructors without dedup, multi merge.
///
/// Extensions beyond C++23 std::flat_map:
///   - reserve(n), shrink_to_fit()        — bulk pre-allocation / compaction
///   - merge(source) (lvalue & rvalue)    — std::map-style element transfer
///   - insert_range(sorted_hint_t, R&&)   — sorted bulk range insert
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
template <typename Key, typename T, typename Compare, typename KeyContainer, typename MappedContainer> class flat_map;
template <typename Key, typename T, typename Compare, typename KeyContainer, typename MappedContainer> class flat_multimap;


namespace detail {

//==============================================================================
// paired_storage — comparator-agnostic synchronized dual-container ops
//
// Serves as the flat_map storage type; flat_set uses a simpler single-container
// storage directly.
//==============================================================================

template <typename KeyContainer, typename MappedContainer>
struct paired_storage
{
    using size_type = std::conditional_t
    <
        ( sizeof( typename KeyContainer::size_type ) <= sizeof( typename MappedContainer::size_type ) ),
        typename KeyContainer   ::size_type,
        typename MappedContainer::size_type
    >;
    using difference_type = std::ptrdiff_t;

    constexpr paired_storage() = default;

    constexpr paired_storage( KeyContainer k, MappedContainer v )
        noexcept( std::is_nothrow_move_constructible_v<KeyContainer> && std::is_nothrow_move_constructible_v<MappedContainer> )
        : keys{ std::move( k ) }, values{ std::move( v ) } {}

    template <std::input_iterator InputIt>
    constexpr paired_storage( InputIt first, InputIt last ) { append_range( std::ranges::subrange( first, last ) ); }

    KeyContainer    keys;
    MappedContainer values;

    //--------------------------------------------------------------------------
    // SCARY iterator (type-independent of comparator)
    //
    // Stores a pointer to the paired_storage + index.  The iterator type
    // depends on KC/MC but NOT on Compare.
    //--------------------------------------------------------------------------
    using key_type    = typename KeyContainer::value_type;
    using mapped_type = typename MappedContainer::value_type;
    using value_type  = ::std::pair<key_type, mapped_type>;

    template <bool IsConst>
    class iterator_impl
    {
    public:
        using iterator_concept  = std::random_access_iterator_tag;
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = std::pair<key_type, mapped_type>;
        using difference_type   = std::ptrdiff_t;
        using reference         = std::pair<key_type const &,
                                            std::conditional_t<IsConst,
                                                mapped_type const &,
                                                mapped_type       &>>;

        struct arrow_proxy {
            reference ref;
            constexpr reference       * operator->()       noexcept { return &ref; }
            constexpr reference const * operator->() const noexcept { return &ref; }
        };
        using pointer = arrow_proxy;

    private:
        friend paired_storage;
        friend iterator_impl<!IsConst>;

        using storage_t = std::conditional_t<IsConst, paired_storage const, paired_storage>;

        storage_t * __restrict storage_{ nullptr };
        size_type              idx_    { 0 };

        constexpr iterator_impl( storage_t * const s, size_type const i ) noexcept
            : storage_{ s }, idx_{ i } {}

    public:
        constexpr iterator_impl() noexcept = default;
        constexpr iterator_impl( iterator_impl const & ) noexcept = default;
        constexpr iterator_impl & operator=( iterator_impl const & ) noexcept = default;

        // Implicit mutable → const conversion
        constexpr iterator_impl( iterator_impl<false> const & other ) noexcept requires IsConst
            : storage_{ other.storage_ }, idx_{ other.idx_ } {}

        [[nodiscard]] constexpr size_type index() const noexcept { return idx_; }

        constexpr reference operator*() const noexcept {
            return { storage_->keys[ idx_ ], storage_->values[ idx_ ] };
        }

        constexpr arrow_proxy operator->() const noexcept { return { **this }; }

        constexpr reference operator[]( difference_type const n ) const noexcept {
            return *( *this + n );
        }

        constexpr iterator_impl & operator++(     ) noexcept { ++idx_; return *this; }
        constexpr iterator_impl   operator++( int ) noexcept { auto tmp{ *this }; ++idx_; return tmp; }
        constexpr iterator_impl & operator--(     ) noexcept { --idx_; return *this; }
        constexpr iterator_impl   operator--( int ) noexcept { auto tmp{ *this }; --idx_; return tmp; }

        constexpr iterator_impl & operator+=( difference_type const n ) noexcept { idx_ = static_cast<size_type>( static_cast<difference_type>( idx_ ) + n ); return *this; }
        constexpr iterator_impl & operator-=( difference_type const n ) noexcept { idx_ = static_cast<size_type>( static_cast<difference_type>( idx_ ) - n ); return *this; }

        friend constexpr iterator_impl operator+( iterator_impl it, difference_type const n ) noexcept { return { it.storage_, static_cast<size_type>( static_cast<difference_type>( it.idx_ ) + n ) }; }
        friend constexpr iterator_impl operator+( difference_type const n, iterator_impl it ) noexcept { return { it.storage_, static_cast<size_type>( static_cast<difference_type>( it.idx_ ) + n ) }; }
        friend constexpr iterator_impl operator-( iterator_impl it, difference_type const n ) noexcept { return { it.storage_, static_cast<size_type>( static_cast<difference_type>( it.idx_ ) - n ) }; }

        friend constexpr difference_type operator-( iterator_impl const & a, iterator_impl const & b ) noexcept { return static_cast<difference_type>( a.idx_ ) - static_cast<difference_type>( b.idx_ ); }

        friend constexpr bool operator== ( iterator_impl const & a, iterator_impl const & b ) noexcept { return a.idx_  == b.idx_; }
        friend constexpr auto operator<=>( iterator_impl const & a, iterator_impl const & b ) noexcept { return a.idx_ <=> b.idx_; }
    }; // iterator_impl

    using iterator       = iterator_impl<false>;
    using const_iterator = iterator_impl<true>;

    constexpr iterator       make_iter( size_type const pos )       noexcept { return { this, pos }; }
    constexpr const_iterator make_iter( size_type const pos ) const noexcept { return { this, pos }; }

    constexpr iterator       begin()       noexcept { return make_iter( 0 ); }
    constexpr const_iterator begin() const noexcept { return make_iter( 0 ); }
    constexpr iterator       end  ()       noexcept { return make_iter( static_cast<size_type>( keys.size() ) ); }
    constexpr const_iterator end  () const noexcept { return make_iter( static_cast<size_type>( keys.size() ) ); }

    //--------------------------------------------------------------------------
    // Zip view for synchronized sort/merge/unique
    //--------------------------------------------------------------------------
    constexpr auto zip_view() noexcept { return std::views::zip( keys, values ); }

    //--------------------------------------------------------------------------
    // Truncate both containers to newSize (shrink-only, noexcept)
    //--------------------------------------------------------------------------
    constexpr void truncate_to( size_type const newSize ) noexcept
    {
        detail::truncate_to( keys,   newSize );
        detail::truncate_to( values, newSize );
    }

    //--------------------------------------------------------------------------
    // Synchronized single-element insert at position (exception-safe)
    //--------------------------------------------------------------------------
    template <typename K, typename V>
    constexpr void insert_element_at( size_type const pos, K && key, V && val ) {
        auto const p{ static_cast<difference_type>( pos ) };
        keys.insert( keys.begin() + p, std::forward<K>( key ) );
        try {
            values.insert( values.begin() + p, std::forward<V>( val ) );
        } catch ( ... ) {
            keys.erase( keys.begin() + p );
            throw;
        }
    }

    //--------------------------------------------------------------------------
    // Synchronized erase
    //--------------------------------------------------------------------------
    constexpr void erase_element_at( size_type const pos ) noexcept
    {
        auto const p{ static_cast<difference_type>( pos ) };
        keys  .erase( keys  .begin() + p );
        values.erase( values.begin() + p );
    }

    constexpr void erase_elements( size_type const first, size_type const last ) noexcept
    {
        auto const f{ static_cast<difference_type>( first ) };
        auto const l{ static_cast<difference_type>( last  ) };
        keys  .erase( keys  .begin() + f, keys  .begin() + l );
        values.erase( values.begin() + f, values.begin() + l );
    }

    //--------------------------------------------------------------------------
    // Bulk append from separate key/value ranges (exception-safe)
    //--------------------------------------------------------------------------
    template <typename KR, typename VR>
    constexpr void append_ranges( KR && key_rg, VR && val_rg )
    {
        auto const oldSize{ keys.size() };
        keys.append_range( std::forward<KR>( key_rg ) );
        try {
            values.append_range( std::forward<VR>( val_rg ) );
        } catch ( ... ) {
            truncate_to( oldSize );
            throw;
        }
    }

    //--------------------------------------------------------------------------
    // Bulk append from a range of pair-like elements (matches vector::append_range)
    //--------------------------------------------------------------------------
    template <std::ranges::input_range R>
    constexpr void append_range( R && rg ) {
        append_ranges( rg | std::views::keys, rg | std::views::values );
    }

    //--------------------------------------------------------------------------
    // Reserve / Shrink
    //--------------------------------------------------------------------------
    constexpr void reserve( size_type const n ) {
        keys  .reserve( n );
        values.reserve( n );
    }

    constexpr void shrink_to_fit() noexcept {
        keys  .shrink_to_fit();
        values.shrink_to_fit();
    }

    //--------------------------------------------------------------------------
    // Replace (move-assign both containers)
    //--------------------------------------------------------------------------
    constexpr void replace( KeyContainer && new_keys, MappedContainer && new_values )
        noexcept( std::is_nothrow_move_assignable_v<KeyContainer> && std::is_nothrow_move_assignable_v<MappedContainer> )
    {
        BOOST_ASSERT( new_keys.size() == new_values.size() );
        keys   = std::move( new_keys   );
        values = std::move( new_values );
    }

    //--------------------------------------------------------------------------
    // Clear / Swap
    //--------------------------------------------------------------------------
    constexpr void clear() noexcept {
        keys  .clear();
        values.clear();
    }

    friend constexpr void swap( paired_storage & a, paired_storage & b ) noexcept {
        using std::swap;
        swap( a.keys,   b.keys   );
        swap( a.values, b.values );
    }

    //--------------------------------------------------------------------------
    // Comparison
    //--------------------------------------------------------------------------
    friend constexpr bool operator==( paired_storage const & a, paired_storage const & b ) noexcept {
        return a.keys == b.keys && a.values == b.values;
    }

    friend constexpr auto operator<=>( paired_storage const & a, paired_storage const & b ) noexcept
    requires std::three_way_comparable<typename KeyContainer::value_type> && std::three_way_comparable<typename MappedContainer::value_type>
    {
        if ( auto const cmp{ std::lexicographical_compare_three_way( a.keys.begin(), a.keys.end(), b.keys.begin(), b.keys.end() ) }; cmp != 0 )
            return cmp;
        return std::lexicographical_compare_three_way( a.values.begin(), a.values.end(), b.values.begin(), b.values.end() );
    }
}; // struct paired_storage


//==============================================================================
// paired_storage overloads of storage abstraction helpers
// (found by ADL at flat_impl template instantiation time)
//==============================================================================

// truncate_to — paired_storage overload (delegates to member)
template <typename KC, typename MC>
constexpr void truncate_to( paired_storage<KC,MC> & s, typename paired_storage<KC,MC>::size_type const n ) noexcept {
    s.truncate_to( n );
}

// keys_of — extract the keys container from paired_storage
template <typename KC, typename MC>
constexpr auto       & keys_of( paired_storage<KC,MC>       & s ) noexcept { return s.keys; }
template <typename KC, typename MC>
constexpr auto const & keys_of( paired_storage<KC,MC> const & s ) noexcept { return s.keys; }

// sort_storage — paired_storage overload (zip-view sort)
template <bool Unique, typename KC, typename MC, typename Comp>
constexpr void sort_storage( paired_storage<KC,MC> & storage, Comp const & comp ) {
    auto zv{ storage.zip_view() };
    std::ranges::sort( zv, comp, key_proj() );
    if constexpr ( Unique ) {
        auto const newEnd{ std::ranges::unique( zv, key_equiv( comp ), key_proj() ).begin() };
        storage.truncate_to( static_cast<typename paired_storage<KC,MC>::size_type>( newEnd - zv.begin() ) );
    }
}

// sort_merge_storage — paired_storage overload (zip-view sort + merge)
template <bool Unique, bool WasSorted, typename KC, typename MC, typename Comp>
constexpr void sort_merge_storage( paired_storage<KC,MC> & storage, Comp const & comp, typename paired_storage<KC,MC>::size_type const oldSize ) {
    if ( storage.keys.size() <= oldSize )
        return;
    auto zv{ storage.zip_view() };
    auto const appendStart{ zv.begin() + static_cast<std::ptrdiff_t>( oldSize ) };

    if constexpr ( !WasSorted )
        std::ranges::sort( appendStart, zv.end(), comp, key_proj() );

    if ( oldSize > 0 )
        std::ranges::inplace_merge( zv.begin(), appendStart, zv.end(), comp, key_proj() );

    if constexpr ( Unique ) {
        auto const newEnd{ std::ranges::unique( zv, key_equiv( comp ), key_proj() ).begin() };
        storage.truncate_to( static_cast<typename paired_storage<KC,MC>::size_type>( newEnd - zv.begin() ) );
    }
}

// storage_erase_at — paired_storage overload
template <typename KC, typename MC>
constexpr void storage_erase_at( paired_storage<KC,MC> & s, typename paired_storage<KC,MC>::size_type const pos ) noexcept {
    s.erase_element_at( pos );
}

// storage_erase_range — paired_storage overload
template <typename KC, typename MC>
constexpr void storage_erase_range( paired_storage<KC,MC> & s, typename paired_storage<KC,MC>::size_type const first, typename paired_storage<KC,MC>::size_type const last ) noexcept {
    s.erase_elements( first, last );
}

// storage_move_append — paired_storage overload
template <typename KC, typename MC>
constexpr void storage_move_append( paired_storage<KC,MC> & dest, paired_storage<KC,MC> & source ) {
    dest.append_ranges( source.keys | std::views::as_rvalue, source.values | std::views::as_rvalue );
}

// storage_emplace_back_from — paired_storage overload
template <typename KC, typename MC>
constexpr void storage_emplace_back_from( paired_storage<KC,MC> & dest, paired_storage<KC,MC> & source, typename paired_storage<KC,MC>::size_type const idx ) {
    dest.keys  .emplace_back( std::move( source.keys  [ idx ] ) );
    dest.values.emplace_back( std::move( source.values[ idx ] ) );
}

// storage_move_element — paired_storage overload
template <typename KC, typename MC>
constexpr void storage_move_element( paired_storage<KC,MC> & s, typename paired_storage<KC,MC>::size_type const dst, typename paired_storage<KC,MC>::size_type const src ) noexcept {
    s.keys  [ dst ] = std::move( s.keys  [ src ] );
    s.values[ dst ] = std::move( s.values[ src ] );
}

// erase_if — paired_storage overload (map path)
// zip_view yields tuple<Key&, Value&>; projection converts to pair so the
// predicate receives pair<Key const &, Value &> as mandated by the standard.
// The projection is required for indirect_unary_predicate constraint
// satisfaction on all three implementations (P2165R4 tuple→pair implicit
// conversion does not satisfy the constraint).
// Exception-safe: clears on predicate exception (basic guarantee) to
// maintain key/value synchronisation.
template <typename KC, typename MC, typename Pred>
constexpr auto erase_if( paired_storage<KC,MC> & s, Pred pred ) {
    try {
        auto zv{ s.zip_view() };
        auto const it{ std::ranges::remove_if( zv, std::move( pred ),
            []( auto && tup ) noexcept {
                return std::pair<typename KC::value_type const &, typename MC::value_type&>{ tup };
            }
        ) };
        auto const newSize{ static_cast<typename paired_storage<KC,MC>::size_type>( it.begin() - zv.begin() ) };
        auto const erased { static_cast<typename paired_storage<KC,MC>::size_type>( s.keys.size() ) - newSize };
        s.truncate_to( newSize );
        return erased;
    } catch ( ... ) {
        s.clear();
        throw;
    }
}

} // namespace detail


//==============================================================================
// flat_map_impl — shared base for flat_map and flat_multimap
//
// Provides everything that does NOT depend on uniqueness semantics:
//   iterators, lookup, positional erase, erase by key,
//   extract/replace, observers, erase_if.
//   Inherits from flat_impl for: storage, comparator, empty, size, clear,
//   reserve, shrink_to_fit, key_comp, comparison, merge, lookup helpers,
//   sort utilities.
//==============================================================================

template
<
    typename Key,
    typename T,
    typename Compare         = std::less<Key>,
    typename KeyContainer    = std::vector<Key>,
    typename MappedContainer = std::vector<T>
>
class flat_map_impl
    : public flat_impl<detail::paired_storage<KeyContainer, MappedContainer>, Compare>
{
    using storage   = detail::paired_storage<KeyContainer, MappedContainer>;
    using flat_base = flat_impl<storage, Compare>;

    static_assert( std::is_same_v<Key, typename KeyContainer   ::value_type>, "KeyContainer::value_type must be Key" );
    static_assert( std::is_same_v<T,   typename MappedContainer::value_type>, "MappedContainer::value_type must be T" );

    template <typename, typename, typename, typename, typename> friend class flat_map;
    template <typename, typename, typename, typename, typename> friend class flat_multimap;

public:
    //--------------------------------------------------------------------------
    // Member types (key_compare, nothrow_move_constructible — inherited)
    //--------------------------------------------------------------------------
    using typename flat_base::size_type;
    using typename flat_base::difference_type;
    using typename flat_base::key_type;
    using mapped_type           = T;
    using value_type            = std::pair<key_type, mapped_type>;
    using reference             = std::pair<key_type const &, mapped_type       &>;
    using const_reference       = std::pair<key_type const &, mapped_type const &>;
    using key_container_type    = KeyContainer;
    using mapped_container_type = MappedContainer;
    using containers            = storage;

    // Inherited from flat_impl → Komparator
    using flat_base::transparent_comparator;
    using typename flat_base::key_const_arg;

    //--------------------------------------------------------------------------
    // value_compare
    //--------------------------------------------------------------------------
    class value_compare : private Compare {
        friend flat_map_impl;
        constexpr value_compare( Compare c ) noexcept( std::is_nothrow_move_constructible_v<Compare> ) : Compare{ std::move( c ) } {}
    public:
        constexpr bool operator()( const_reference a, const_reference b ) const noexcept { return Compare::operator()( a.first, b.first ); }
    };

    //--------------------------------------------------------------------------
    // Iterator — SCARY: type depends on KC/MC, not on Compare
    //--------------------------------------------------------------------------
public:
    using iterator               = typename storage::iterator;
    using const_iterator         = typename storage::const_iterator;
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    //--------------------------------------------------------------------------
    // Iterators (cbegin/cend, rbegin/rend, crbegin/crend inherited from flat_impl)
    //--------------------------------------------------------------------------
    constexpr auto begin( this auto && self ) noexcept { return self.storage_.begin(); }
    constexpr auto end  ( this auto && self ) noexcept { return self.storage_.end  (); }

    //--------------------------------------------------------------------------
    // Capacity (empty, size, reserve, shrink_to_fit — inherited from flat_impl)
    //--------------------------------------------------------------------------
    [[nodiscard]] constexpr size_type max_size() const noexcept { return std::min( this->storage_.keys.max_size(), this->storage_.values.max_size() ); }

    //--------------------------------------------------------------------------
    // Lookup — find, contains, count, lower_bound, upper_bound, equal_range
    // are all inherited from flat_impl.
    //--------------------------------------------------------------------------

    //--------------------------------------------------------------------------
    // Modifiers — erase
    //--------------------------------------------------------------------------
    constexpr iterator erase( const_iterator const pos ) noexcept { return this->erase_pos_impl( pos ); }
    constexpr iterator erase(       iterator const pos ) noexcept { return erase( const_iterator{ pos } ); }
    constexpr iterator erase( const_iterator const first, const_iterator const last ) noexcept { return this->erase_range_impl( first, last ); }
    template <LookupType<transparent_comparator, key_type> K = key_type>
    constexpr size_type erase( K const & key ) noexcept {
        return this->erase_by_key_impl( key );
    }

    //--------------------------------------------------------------------------
    // Extraction & replacement (extract, keys inherited from flat_impl)
    //--------------------------------------------------------------------------
    constexpr void replace( KeyContainer && new_keys, MappedContainer && new_values )
        noexcept( std::is_nothrow_move_assignable_v<KeyContainer> && std::is_nothrow_move_assignable_v<MappedContainer> )
    {
        this->storage_.replace( std::move( new_keys ), std::move( new_values ) );
    }

    //--------------------------------------------------------------------------
    // Observers (key_comp, key_comp_mutable — inherited from flat_impl)
    //--------------------------------------------------------------------------
    [[nodiscard]] constexpr value_compare value_comp() const noexcept { return value_compare{ this->key_comp() }; }

    [[nodiscard]] constexpr mapped_container_type const & values() const noexcept { return this->storage_.values; }
    [[nodiscard]] constexpr std::span<mapped_type>        values()       noexcept { return this->storage_.values; }

    // erase_if: inherited from flat_impl (friend, found via ADL)

    // Key-container iterator → map iterator (compute index, construct zip iterator)
    constexpr auto iter_from_key( this auto && self, auto const key_it ) noexcept {
        auto const idx{ static_cast<size_type>( key_it - self.storage_.keys.begin() ) };
        return self.make_iter( idx );
    }

    // Iterator factory from index position (const/non-const dispatched via storage_)
    constexpr auto make_iter( this auto && self, size_type const pos ) noexcept {
        return self.storage_.make_iter( pos );
    }

    // Iterator → index conversion
    constexpr size_type iter_index( const_iterator const it ) const noexcept {
        return it.index();
    }

    //--------------------------------------------------------------------------
    // Protected interface for derived classes
    //--------------------------------------------------------------------------
protected:
    constexpr flat_map_impl() = default;

    constexpr explicit flat_map_impl( Compare const & comp ) noexcept( std::is_nothrow_copy_constructible_v<Compare> )
        : flat_base{ comp } {}

    constexpr flat_map_impl( Compare const & comp, KeyContainer keys, MappedContainer values ) noexcept( flat_base::nothrow_move_constructible )
        : flat_base{ comp, storage{ std::move( keys ), std::move( values ) } }
    {
        BOOST_ASSERT( this->storage_.keys.size() == this->storage_.values.size() );
    }

    // "Fake deducing-this" forwarder — passes Derived & through to flat_impl
    template <typename Derived, typename... Args>
    requires std::derived_from<std::remove_cvref_t<Derived>, flat_map_impl>
    constexpr flat_map_impl( Derived & self, Args &&... args )
        : flat_base{ self, std::forward<Args>( args )... } {}

    // Pre-sorted iterator pair (no Derived needed — no sorting)
    template <std::input_iterator InputIt>
    constexpr flat_map_impl( Compare const & comp, InputIt first, InputIt last )
        : flat_base{ comp, first, last } {}

    constexpr flat_map_impl( flat_map_impl const & ) = default;
    constexpr flat_map_impl( flat_map_impl && )      = default;
    constexpr flat_map_impl & operator=( flat_map_impl const & ) = default;
    constexpr flat_map_impl & operator=( flat_map_impl && )      = default;
}; // class flat_map_impl


//==============================================================================
// flat_map — unique sorted map
//==============================================================================

template
<
    typename Key,
    typename T,
    typename Compare         = std::less<Key>,
    typename KeyContainer    = std::vector<Key>,
    typename MappedContainer = std::vector<T>
>
class flat_map
    : public flat_map_impl<Key, T, Compare, KeyContainer, MappedContainer>
{
    using base = flat_map_impl<Key, T, Compare, KeyContainer, MappedContainer>;
    using typename base::difference_type;


public:
    using typename base::key_type;
    using typename base::mapped_type;
    using typename base::value_type;
    using typename base::size_type;
    using typename base::iterator;
    using typename base::const_iterator;
    using typename base::const_reference;
    using sorted_hint_t = sorted_unique_t;
    static constexpr bool unique{ true };
    using base::transparent_comparator;

    //--------------------------------------------------------------------------
    // Constructors — one-liners via "fake deducing-this" base constructors
    //--------------------------------------------------------------------------
    constexpr flat_map() = default;

    constexpr explicit flat_map( Compare const & comp ) noexcept( std::is_nothrow_copy_constructible_v<Compare> )
        : base{ comp } {}

    constexpr flat_map( KeyContainer keys, MappedContainer values, Compare const & comp = Compare{} )
        : base{ *this, comp, typename base::containers{ std::move( keys ), std::move( values ) } } {}

    constexpr flat_map( sorted_unique_t, KeyContainer keys, MappedContainer values, Compare const & comp = Compare{} ) noexcept( base::nothrow_move_constructible )
        : base{ comp, std::move( keys ), std::move( values ) } {}

    template <std::input_iterator InputIt>
    constexpr flat_map( InputIt first, InputIt const last, Compare const & comp = Compare{} )
        : base{ *this, comp, first, last } {}

    template <std::input_iterator InputIt>
    constexpr flat_map( sorted_unique_t, InputIt first, InputIt const last, Compare const & comp = Compare{} )
        : base{ comp, first, last } {}

    template <std::ranges::input_range R>
    constexpr flat_map( std::from_range_t tag, R && rg, Compare const & comp = Compare{} )
        : base{ *this, comp, tag, std::forward<R>( rg ) } {}

    constexpr flat_map( std::initializer_list<value_type> const il, Compare const & comp = Compare{} )
        : flat_map( il.begin(), il.end(), comp ) {}

    constexpr flat_map( sorted_unique_t s, std::initializer_list<value_type> il, Compare const & comp = Compare{} )
        : flat_map( s, il.begin(), il.end(), comp ) {}

    constexpr flat_map( flat_map const & ) = default;
    constexpr flat_map( flat_map && )      = default;

    constexpr flat_map & operator=( flat_map const & ) = default;
    constexpr flat_map & operator=( flat_map && )      = default;

    constexpr flat_map & operator=( std::initializer_list<value_type> il ) {
        this->assign( il );
        return *this;
    }

    //--------------------------------------------------------------------------
    // Swap (type-safe: only flat_map ↔ flat_map, not flat_map ↔ flat_multimap)
    //--------------------------------------------------------------------------
    constexpr void swap( flat_map & other ) noexcept { this->swap_impl( other ); }
    friend constexpr void swap( flat_map & a, flat_map & b ) noexcept { a.swap( b ); }

    //--------------------------------------------------------------------------
    // Element access
    //--------------------------------------------------------------------------
    constexpr mapped_type & operator[]( key_type const & key ) {
        return try_emplace( key ).first->second;
    }

    constexpr mapped_type & operator[]( key_type && key ) {
        return try_emplace( std::move( key ) ).first->second;
    }

    template <typename K> requires( transparent_comparator )
    constexpr mapped_type & operator[]( K && key ) {
        return try_emplace( std::forward<K>( key ) ).first->second;
    }

    template <LookupType<transparent_comparator, key_type> K = key_type>
    constexpr auto & at( this auto && self, K const & key ) {
        auto const it{ self.find( key ) };
        if ( it == self.end() )
            detail::throw_out_of_range( "psi::vm::flat_map::at" );
        return it->second;
    }

    //--------------------------------------------------------------------------
    // Lookup — find, contains, count, lower_bound, upper_bound, equal_range
    // inherited from flat_impl. count() uses self.unique for optimization.
    //--------------------------------------------------------------------------

    //--------------------------------------------------------------------------
    // Modifiers — optimized erase by key for unique keys
    //--------------------------------------------------------------------------
#ifndef _MSC_VER
    using base::erase; // positional + range + by-key from flat_map_impl
#else
    // MSVC workaround: [namespace.udecl]/15 hiding not applied — see flat_set.
    constexpr iterator erase( const_iterator const pos ) noexcept { return base::erase( pos ); }
    constexpr iterator erase(       iterator const pos ) noexcept { return base::erase( pos ); }
    constexpr iterator erase( const_iterator const first, const_iterator const last ) noexcept { return base::erase( first, last ); }
#endif

    template <LookupType<transparent_comparator, key_type> K = key_type>
    constexpr size_type erase( K const & key ) noexcept {
        auto const it{ this->find( key ) };
        if ( it == this->end() ) return 0;
        detail::storage_erase_at( this->storage_, this->iter_index( const_iterator{ it } ) );
        return 1;
    }

    //--------------------------------------------------------------------------
    // Modifiers — unique insert / emplace
    //--------------------------------------------------------------------------
    using base::insert;       // bulk insert, initializer_list, sorted bulk — from flat_impl
    using base::insert_range; // insert_range, sorted insert_range — from flat_impl

    constexpr auto insert( value_type const & v ) { return emplace( v.first, v.second ); }
    constexpr auto insert( value_type &&      v ) { return emplace( std::move( v.first ), std::move( v.second ) ); }

    constexpr iterator insert( const_iterator hint, value_type const & v ) { return emplace_hint( hint, v.first, v.second ); }
    constexpr iterator insert( const_iterator hint, value_type &&      v ) { return emplace_hint( hint, std::move( v.first ), std::move( v.second ) ); }

    template <typename K, typename... Args>
    constexpr std::pair<iterator, bool> try_emplace( K && key, Args &&... args ) {
        auto const pos{ this->lower_bound_index( key ) };
        if ( this->key_eq_at( pos, key ) )
            return { this->make_iter( pos ), false };
        this->storage_.insert_element_at( pos, key_type( std::forward<K>( key ) ), mapped_type( std::forward<Args>( args )... ) );
        return { this->make_iter( pos ), true };
    }

    template <typename K, typename... Args>
    constexpr iterator try_emplace( const_iterator hint, K && key, Args &&... args ) {
        auto const hintIdx{ this->iter_index( hint ) };
        bool const hintValid{
            ( hintIdx == 0            || this->le( this->storage_.keys[ hintIdx - 1 ], key ) ) &&
            ( hintIdx >= this->size() || this->le( key, this->storage_.keys[ hintIdx ] ) )
        };
        if ( hintValid ) {
            this->storage_.insert_element_at( hintIdx, key_type( std::forward<K>( key ) ), mapped_type( std::forward<Args>( args )... ) );
            return this->make_iter( hintIdx );
        }
        if ( hintIdx < this->size() && this->eq( key, this->storage_.keys[ hintIdx ] ) )
            return this->make_iter( hintIdx );
        return try_emplace( std::forward<K>( key ), std::forward<Args>( args )... ).first;
    }

    template <typename K, typename M>
    constexpr std::pair<iterator, bool> insert_or_assign( K && key, M && value ) {
        auto const pos{ this->lower_bound_index( key ) };
        if ( this->key_eq_at( pos, key ) ) {
            this->storage_.values[ pos ] = std::forward<M>( value );
            return { this->make_iter( pos ), false };
        }
        this->storage_.insert_element_at( pos, key_type( std::forward<K>( key ) ), std::forward<M>( value ) );
        return { this->make_iter( pos ), true };
    }

    template <typename K, typename M>
    constexpr iterator insert_or_assign( const_iterator hint, K && key, M && value ) {
        auto const hintIdx{ this->iter_index( hint ) };
        if ( hintIdx < this->size() && this->eq( key, this->storage_.keys[ hintIdx ] ) ) {
            this->storage_.values[ hintIdx ] = std::forward<M>( value );
            return this->make_iter( hintIdx );
        }
        return insert_or_assign( std::forward<K>( key ), std::forward<M>( value ) ).first;
    }

    template <typename... Args>
    constexpr std::pair<iterator, bool> emplace( Args &&... args ) {
        value_type v( std::forward<Args>( args )... );
        return try_emplace( std::move( v.first ), std::move( v.second ) );
    }
    template <typename... Args>
    constexpr iterator emplace_hint( const_iterator hint, Args &&... args ) {
        value_type v( std::forward<Args>( args )... );
        return try_emplace( hint, std::move( v.first ), std::move( v.second ) );
    }
}; // class flat_map


//==============================================================================
// flat_multimap — sorted map with equivalent keys allowed
//==============================================================================

template
<
    typename Key,
    typename T,
    typename Compare         = std::less<Key>,
    typename KeyContainer    = std::vector<Key>,
    typename MappedContainer = std::vector<T>
>
class flat_multimap
    : public flat_map_impl<Key, T, Compare, KeyContainer, MappedContainer>
{
    using base = flat_map_impl<Key, T, Compare, KeyContainer, MappedContainer>;
    using typename base::difference_type;


public:
    using typename base::key_type;
    using typename base::mapped_type;
    using typename base::value_type;
    using typename base::size_type;
    using typename base::iterator;
    using typename base::const_iterator;
    using sorted_hint_t = sorted_equivalent_t;
    static constexpr bool unique{ false };

    //--------------------------------------------------------------------------
    // Constructors — one-liners via "fake deducing-this" base constructors
    //--------------------------------------------------------------------------
    constexpr flat_multimap() = default;

    constexpr explicit flat_multimap( Compare const & comp ) noexcept( std::is_nothrow_copy_constructible_v<Compare> )
        : base{ comp } {}

    constexpr flat_multimap( KeyContainer keys, MappedContainer values, Compare const & comp = Compare{} )
        : base{ *this, comp, typename base::containers{ std::move( keys ), std::move( values ) } } {}

    constexpr flat_multimap( sorted_equivalent_t, KeyContainer keys, MappedContainer values, Compare const & comp = Compare{} ) noexcept( base::nothrow_move_constructible )
        : base{ comp, std::move( keys ), std::move( values ) } {}

    template <std::input_iterator InputIt>
    constexpr flat_multimap( InputIt first, InputIt const last, Compare const & comp = Compare{} )
        : base{ *this, comp, first, last } {}

    template <std::input_iterator InputIt>
    constexpr flat_multimap( sorted_equivalent_t, InputIt first, InputIt const last, Compare const & comp = Compare{} )
        : base{ comp, first, last } {}

    template <std::ranges::input_range R>
    constexpr flat_multimap( std::from_range_t tag, R && rg, Compare const & comp = Compare{} )
        : base{ *this, comp, tag, std::forward<R>( rg ) } {}

    constexpr flat_multimap( std::initializer_list<value_type> const il, Compare const & comp = Compare{} )
        : flat_multimap( il.begin(), il.end(), comp ) {}

    constexpr flat_multimap( sorted_equivalent_t s, std::initializer_list<value_type> il, Compare const & comp = Compare{} )
        : flat_multimap( s, il.begin(), il.end(), comp ) {}

    constexpr flat_multimap( flat_multimap const & ) = default;
    constexpr flat_multimap( flat_multimap && )      = default;

    constexpr flat_multimap & operator=( flat_multimap const & ) = default;
    constexpr flat_multimap & operator=( flat_multimap && )      = default;

    constexpr flat_multimap & operator=( std::initializer_list<value_type> il ) {
        this->assign( il );
        return *this;
    }

    //--------------------------------------------------------------------------
    // Swap (type-safe)
    //--------------------------------------------------------------------------
    constexpr void swap( flat_multimap & other ) noexcept { this->swap_impl( other ); }
    friend constexpr void swap( flat_multimap & a, flat_multimap & b ) noexcept { a.swap( b ); }

    //--------------------------------------------------------------------------
    // Modifiers — multi insert / emplace
    //--------------------------------------------------------------------------
    using base::insert;       // bulk insert, initializer_list, sorted bulk — from flat_impl
    using base::insert_range; // insert_range, sorted insert_range — from flat_impl

    constexpr auto insert( value_type const & v ) { return emplace( v.first, v.second ); }
    constexpr auto insert( value_type &&      v ) { return emplace( std::move( v.first ), std::move( v.second ) ); }

    constexpr iterator insert( const_iterator hint, value_type const & v ) { return emplace_hint( hint, v.first, v.second ); }
    constexpr iterator insert( const_iterator hint, value_type &&      v ) { return emplace_hint( hint, std::move( v.first ), std::move( v.second ) ); }

    template <typename... Args>
    constexpr iterator emplace( Args &&... args ) {
        value_type v( std::forward<Args>( args )... );
        auto const pos{ this->lower_bound_index( v.first ) };
        this->storage_.insert_element_at( pos, std::move( v.first ), std::move( v.second ) );
        return this->make_iter( pos );
    }

    constexpr iterator emplace_hint( const_iterator hint, auto &&... args ) {
        value_type v( std::forward<decltype( args )>( args )... );
        auto const pos{ this->hinted_insert_pos( this->iter_index( hint ), enreg( v.first ) ) };
        this->storage_.insert_element_at( pos, std::move( v.first ), std::move( v.second ) );
        return this->make_iter( pos );
    }

}; // class flat_multimap


//------------------------------------------------------------------------------
// Deduction guides — flat_map
//------------------------------------------------------------------------------

// Container pair (unsorted)
template <typename KC, typename MC, typename Comp = std::less<typename KC::value_type>>
requires( !std::is_same_v<KC, sorted_unique_t> && !std::is_same_v<KC, sorted_equivalent_t> )
flat_map( KC, MC, Comp = Comp{} )
    -> flat_map<typename KC::value_type, typename MC::value_type, Comp, KC, MC>;

// Container pair (sorted unique)
template <typename KC, typename MC, typename Comp = std::less<typename KC::value_type>>
flat_map( sorted_unique_t, KC, MC, Comp = Comp{} )
    -> flat_map<typename KC::value_type, typename MC::value_type, Comp, KC, MC>;

// Iterator range (unsorted)
template <std::input_iterator InputIt, typename Comp = std::less<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type::first_type>>>
requires( !std::is_same_v<InputIt, sorted_unique_t> && !std::is_same_v<InputIt, sorted_equivalent_t> )
flat_map( InputIt, InputIt, Comp = Comp{} )
    -> flat_map<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type::first_type>,
                typename std::iterator_traits<InputIt>::value_type::second_type,
                Comp>;

// Iterator range (sorted unique)
template <std::input_iterator InputIt, typename Comp = std::less<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type::first_type>>>
flat_map( sorted_unique_t, InputIt, InputIt, Comp = Comp{} )
    -> flat_map<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type::first_type>,
                typename std::iterator_traits<InputIt>::value_type::second_type,
                Comp>;

// from_range_t
template <std::ranges::input_range R, typename Comp = std::less<std::remove_const_t<typename std::ranges::range_value_t<R>::first_type>>>
flat_map( std::from_range_t, R &&, Comp = Comp{} )
    -> flat_map<std::remove_const_t<typename std::ranges::range_value_t<R>::first_type>,
                typename std::ranges::range_value_t<R>::second_type,
                Comp>;

// Initializer list (unsorted)
template <typename Key, typename T, typename Comp = std::less<Key>>
flat_map( std::initializer_list<std::pair<Key, T>>, Comp = Comp{} )
    -> flat_map<Key, T, Comp>;

// Initializer list (sorted unique)
template <typename Key, typename T, typename Comp = std::less<Key>>
flat_map( sorted_unique_t, std::initializer_list<std::pair<Key, T>>, Comp = Comp{} )
    -> flat_map<Key, T, Comp>;


//------------------------------------------------------------------------------
// Deduction guides — flat_multimap
//------------------------------------------------------------------------------

// Container pair (unsorted)
template <typename KC, typename MC, typename Comp = std::less<typename KC::value_type>>
requires( !std::is_same_v<KC, sorted_unique_t> && !std::is_same_v<KC, sorted_equivalent_t> )
flat_multimap( KC, MC, Comp = Comp{} )
    -> flat_multimap<typename KC::value_type, typename MC::value_type, Comp, KC, MC>;

// Container pair (sorted equivalent)
template <typename KC, typename MC, typename Comp = std::less<typename KC::value_type>>
flat_multimap( sorted_equivalent_t, KC, MC, Comp = Comp{} )
    -> flat_multimap<typename KC::value_type, typename MC::value_type, Comp, KC, MC>;

// Iterator range (unsorted)
template <std::input_iterator InputIt, typename Comp = std::less<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type::first_type>>>
requires( !std::is_same_v<InputIt, sorted_unique_t> && !std::is_same_v<InputIt, sorted_equivalent_t> )
flat_multimap( InputIt, InputIt, Comp = Comp{} )
    -> flat_multimap<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type::first_type>,
                typename std::iterator_traits<InputIt>::value_type::second_type,
                Comp>;

// Iterator range (sorted equivalent)
template <std::input_iterator InputIt, typename Comp = std::less<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type::first_type>>>
flat_multimap( sorted_equivalent_t, InputIt, InputIt, Comp = Comp{} )
    -> flat_multimap<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type::first_type>,
                typename std::iterator_traits<InputIt>::value_type::second_type,
                Comp>;

// from_range_t
template <std::ranges::input_range R, typename Comp = std::less<std::remove_const_t<typename std::ranges::range_value_t<R>::first_type>>>
flat_multimap( std::from_range_t, R &&, Comp = Comp{} )
    -> flat_multimap<std::remove_const_t<typename std::ranges::range_value_t<R>::first_type>,
                typename std::ranges::range_value_t<R>::second_type,
                Comp>;

// Initializer list (unsorted)
template <typename Key, typename T, typename Comp = std::less<Key>>
flat_multimap( std::initializer_list<std::pair<Key, T>>, Comp = Comp{} )
    -> flat_multimap<Key, T, Comp>;

// Initializer list (sorted equivalent)
template <typename Key, typename T, typename Comp = std::less<Key>>
flat_multimap( sorted_equivalent_t, std::initializer_list<std::pair<Key, T>>, Comp = Comp{} )
    -> flat_multimap<Key, T, Comp>;


//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------

template <typename Key, typename Value, typename Compare, typename KC, typename MC>
bool constexpr psi::vm::is_trivially_moveable<psi::vm::flat_map<Key, Value, Compare, KC, MC>>{ psi::vm::is_trivially_moveable<Compare> };

template <typename Key, typename Value, typename Compare, typename KC, typename MC>
bool constexpr psi::vm::is_trivially_moveable<psi::vm::flat_multimap<Key, Value, Compare, KC, MC>>{ psi::vm::is_trivially_moveable<Compare> };
//------------------------------------------------------------------------------
