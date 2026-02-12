////////////////////////////////////////////////////////////////////////////////
/// std::flat_map polyfill for platforms lacking <flat_map> (notably MSVC STL)
///
/// Implements the C++23 std::flat_map interface (P0429R9) with separate key
/// and value containers for cache-friendly key-only iteration.
///
/// Architecture:
///   flat_map privately inherits detail::paired_storage<KC, MC>, a comparator-
///   agnostic base that synchronises dual-container operations (insert, erase,
///   append, clear, reserve, replace, ...). The base class doubles as the
///   standard-required `containers` type returned by extract().
///
/// Extensions beyond C++23 std::flat_map:
///   - reserve(n), shrink_to_fit()        — bulk pre-allocation / compaction
///   - merge(source) (lvalue & rvalue)    — std::map-style element transfer
///   - insert_range(sorted_unique_t, R&&) — sorted bulk range insert
///
/// Differences from the standard interface:
///   - Uses psi::vm::sorted_unique_t instead of std::sorted_unique_t
///   - containers type = paired_storage base (aggregate with .keys / .values),
///     not a separate nested struct
///   - extract() returns by move-constructing the base (conditionally noexcept)
///   - replace() is conditionally noexcept (move-assignable containers)
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

#include "abi.hpp"

#include <boost/assert.hpp>

#include <algorithm>
#include <compare>
#include <concepts>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

//==============================================================================
// detail::paired_storage — comparator-agnostic synchronized dual-container ops
// Serves as the flat_map/flat_set divergence point (flat_set uses a simpler
// single-container equivalent).
//==============================================================================
namespace detail {

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

    KeyContainer    keys;
    MappedContainer values;

    //--------------------------------------------------------------------------
    // Zip view for synchronized sort/merge/unique
    //--------------------------------------------------------------------------
    auto zip_view() noexcept { return std::views::zip( keys, values ); }

    //--------------------------------------------------------------------------
    // Truncate both containers to newSize (shrink-only, noexcept)
    //--------------------------------------------------------------------------
    void truncate_to( size_type const newSize ) noexcept
    {
        if constexpr ( requires { keys.shrink_to( newSize ); } )
            keys.shrink_to( newSize );
        else
            keys.resize( newSize );
        if constexpr ( requires { values.shrink_to( newSize ); } )
            values.shrink_to( newSize );
        else
            values.resize( newSize );
    }

    //--------------------------------------------------------------------------
    // Synchronized single-element insert at position (exception-safe)
    //--------------------------------------------------------------------------
    template <typename K, typename V>
    void insert_element_at( size_type const pos, K && key, V && val ) {
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
    void erase_element_at( size_type const pos ) noexcept {
        auto const p{ static_cast<difference_type>( pos ) };
        keys  .erase( keys  .begin() + p );
        values.erase( values.begin() + p );
    }

    void erase_elements( size_type const first, size_type const last ) noexcept {
        auto const f{ static_cast<difference_type>( first ) };
        auto const l{ static_cast<difference_type>( last  ) };
        keys  .erase( keys  .begin() + f, keys  .begin() + l );
        values.erase( values.begin() + f, values.begin() + l );
    }

    //--------------------------------------------------------------------------
    // Bulk append from separate key/value ranges (exception-safe)
    //--------------------------------------------------------------------------
    template <typename KR, typename VR>
    void append_ranges( KR && key_rg, VR && val_rg ) {
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
    // Bulk append from pair iterator range
    //--------------------------------------------------------------------------
    template <typename InputIt>
    void append_from( InputIt const first, InputIt const last ) {
        auto rg{ std::ranges::subrange( first, last ) };
        append_ranges( rg | std::views::keys, rg | std::views::values );
    }

    //--------------------------------------------------------------------------
    // Move-append from raw container pair (for rvalue merge)
    //--------------------------------------------------------------------------
    void append_move_containers( KeyContainer & src_keys, MappedContainer & src_values ) {
        append_ranges
        (
            src_keys   | std::views::as_rvalue,
            src_values | std::views::as_rvalue
        );
    }

    //--------------------------------------------------------------------------
    // Reserve / Shrink
    //--------------------------------------------------------------------------
    void reserve( size_type const n ) {
        keys  .reserve( n );
        values.reserve( n );
    }

    void shrink_to_fit() noexcept {
        keys  .shrink_to_fit();
        values.shrink_to_fit();
    }

    //--------------------------------------------------------------------------
    // Replace (move-assign both containers)
    //--------------------------------------------------------------------------
    void replace( KeyContainer new_keys, MappedContainer new_values )
        noexcept( std::is_nothrow_move_assignable_v<KeyContainer> && std::is_nothrow_move_assignable_v<MappedContainer> )
    {
        BOOST_ASSERT( new_keys.size() == new_values.size() );
        keys   = std::move( new_keys   );
        values = std::move( new_values );
    }

    //--------------------------------------------------------------------------
    // Clear / Swap
    //--------------------------------------------------------------------------
    void clear() noexcept {
        keys  .clear();
        values.clear();
    }

    void swap_storage( paired_storage & other ) noexcept {
        using std::swap;
        swap( keys,   other.keys   );
        swap( values, other.values );
    }
}; // struct paired_storage

} // namespace detail


struct sorted_unique_t { explicit sorted_unique_t() = default; };
inline constexpr sorted_unique_t sorted_unique{};

//==============================================================================
// flat_map — sorted associative container with separate key/value storage
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
    : private detail::paired_storage<KeyContainer, MappedContainer>
{
    using base = detail::paired_storage<KeyContainer, MappedContainer>;

    static_assert( std::is_same_v<Key, typename KeyContainer   ::value_type>, "KeyContainer::value_type must be Key" );
    static_assert( std::is_same_v<T,   typename MappedContainer::value_type>, "MappedContainer::value_type must be T" );

public:
    //--------------------------------------------------------------------------
    // Member types
    //--------------------------------------------------------------------------
    using key_type              = Key;
    using mapped_type           = T;
    using value_type            = std::pair<key_type, mapped_type>;
    using key_compare           = Compare;
    using reference             = std::pair<key_type const &, mapped_type       &>;
    using const_reference       = std::pair<key_type const &, mapped_type const &>;
    using size_type             = typename base::size_type;
    using difference_type       = typename base::difference_type;
    using key_container_type    = KeyContainer;
    using mapped_container_type = MappedContainer;
    using containers            = base;

    //--------------------------------------------------------------------------
    // value_compare
    //--------------------------------------------------------------------------
    class value_compare : private key_compare {
        friend flat_map;
        value_compare( key_compare c ) noexcept( std::is_nothrow_move_constructible_v<key_compare> ) : key_compare{ std::move( c ) } {}
    public:
        bool operator()( const_reference a, const_reference b ) const noexcept { return key_compare::operator()( a.first, b.first ); }
    };

    //--------------------------------------------------------------------------
    // Iterator
    //--------------------------------------------------------------------------
private:
    template <bool IsConst>
    class iterator_impl
    {
    public:
        using iterator_concept  = std::random_access_iterator_tag;
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = flat_map::value_type;
        using difference_type   = flat_map::difference_type;
        using reference         = std::conditional_t<IsConst, flat_map::const_reference, flat_map::reference>;

        struct arrow_proxy {
            reference ref;
            constexpr reference       * operator->()       noexcept { return &ref; }
            constexpr reference const * operator->() const noexcept { return &ref; }
        };
        using pointer = arrow_proxy;

    private:
        friend flat_map;
        friend iterator_impl<!IsConst>;

        using map_ptr = std::conditional_t<IsConst, flat_map const *, flat_map *>;

        map_ptr         map_{ nullptr };
        difference_type idx_{ 0 };

        constexpr iterator_impl( map_ptr const m, difference_type const i ) noexcept : map_{ m }, idx_{ i } {}

    public:
        constexpr iterator_impl() noexcept = default;
        constexpr iterator_impl( iterator_impl const & ) noexcept = default;
        constexpr iterator_impl & operator=( iterator_impl const & ) noexcept = default;

        constexpr iterator_impl( iterator_impl<!IsConst> const & other ) noexcept requires IsConst
            : map_{ other.map_ }, idx_{ other.idx_ } {}

        constexpr reference operator*() const noexcept {
            return { map_->base::keys[ static_cast<size_type>( idx_ ) ], map_->base::values[ static_cast<size_type>( idx_ ) ] };
        }

        constexpr arrow_proxy operator->() const noexcept { return { **this }; }

        constexpr reference operator[]( difference_type const n ) const noexcept {
            return *( *this + n );
        }

        constexpr iterator_impl & operator++(     )    noexcept { ++idx_; return *this; }
        constexpr iterator_impl   operator++( int ) noexcept { auto tmp{ *this }; ++idx_; return tmp; }
        constexpr iterator_impl & operator--(     )    noexcept { --idx_; return *this; }
        constexpr iterator_impl   operator--( int ) noexcept { auto tmp{ *this }; --idx_; return tmp; }

        constexpr iterator_impl & operator+=( difference_type const n ) noexcept { idx_ += n; return *this; }
        constexpr iterator_impl & operator-=( difference_type const n ) noexcept { idx_ -= n; return *this; }

        friend constexpr iterator_impl operator+( iterator_impl it, difference_type const n ) noexcept { return { it.map_, it.idx_ + n }; }
        friend constexpr iterator_impl operator+( difference_type const n, iterator_impl it ) noexcept { return { it.map_, it.idx_ + n }; }
        friend constexpr iterator_impl operator-( iterator_impl it, difference_type const n ) noexcept { return { it.map_, it.idx_ - n }; }

        friend constexpr difference_type operator-( iterator_impl const & a, iterator_impl const & b ) noexcept { return a.idx_ - b.idx_; }

        friend constexpr bool operator==( iterator_impl const & a, iterator_impl const & b ) noexcept { return a.idx_ == b.idx_; }
        friend constexpr auto operator<=>( iterator_impl const & a, iterator_impl const & b ) noexcept { return a.idx_ <=> b.idx_; }
    }; // iterator_impl

public:
    using iterator               = iterator_impl<false>;
    using const_iterator         = iterator_impl<true>;
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    //--------------------------------------------------------------------------
    // Constructors
    //--------------------------------------------------------------------------

    static auto constexpr nothrow_move_constructible
    {
        std::is_nothrow_move_constructible_v<KeyContainer   > &&
        std::is_nothrow_move_constructible_v<MappedContainer> &&
        std::is_nothrow_copy_constructible_v<Compare        >
    };

    flat_map() = default;

    explicit flat_map( Compare const & comp ) noexcept( std::is_nothrow_copy_constructible_v<Compare> )
        : comp_{ comp } {}

    flat_map( KeyContainer keys, MappedContainer values, Compare const & comp = Compare{} ) noexcept( nothrow_move_constructible )
        : base{ std::move( keys ), std::move( values ) }, comp_{ comp }
    {
        BOOST_ASSERT( base::keys.size() == base::values.size() );
        sort_and_unique();
    }

    flat_map( sorted_unique_t, KeyContainer keys, MappedContainer values, Compare const & comp = Compare{} ) noexcept( nothrow_move_constructible )
        : base{ std::move( keys ), std::move( values ) }, comp_{ comp }
    {
        BOOST_ASSERT( base::keys.size() == base::values.size() );
    }

    template <std::input_iterator InputIt>
    flat_map( InputIt first, InputIt const last, Compare const & comp = Compare{} )
        : comp_{ comp }
    {
        base::append_from( first, last );
        sort_and_unique();
    }

    template <std::input_iterator InputIt>
    flat_map( sorted_unique_t, InputIt first, InputIt const last, Compare const & comp = Compare{} )
        : comp_{ comp }
    {
        base::append_from( first, last );
    }

    template <std::ranges::input_range R>
    flat_map( std::from_range_t, R && rg, Compare const & comp = Compare{} )
        : comp_{ comp }
    {
        insert_range( std::forward<R>( rg ) );
    }

    flat_map( std::initializer_list<value_type> const il, Compare const & comp = Compare{} )
        : flat_map( il.begin(), il.end(), comp ) {}

    flat_map( sorted_unique_t, std::initializer_list<value_type> il, Compare const & comp = Compare{} )
        : flat_map( sorted_unique, il.begin(), il.end(), comp ) {}

    flat_map( flat_map const & ) = default;
    flat_map( flat_map && )      = default;

    flat_map & operator=( flat_map const & ) = default;
    flat_map & operator=( flat_map && )      = default;

    flat_map & operator=( std::initializer_list<value_type> il ) {
        clear();
        insert( il );
        return *this;
    }

    //--------------------------------------------------------------------------
    // Iterators
    //--------------------------------------------------------------------------
    iterator       begin()       noexcept { return { this, 0 }; }
    const_iterator begin() const noexcept { return { this, 0 }; }
    iterator       end  ()       noexcept { return { this, static_cast<difference_type>( base::keys.size() ) }; }
    const_iterator end  () const noexcept { return { this, static_cast<difference_type>( base::keys.size() ) }; }

    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend  () const noexcept { return end  (); }

    reverse_iterator       rbegin()       noexcept { return reverse_iterator      { end  () }; }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator{ end  () }; }
    reverse_iterator       rend  ()       noexcept { return reverse_iterator      { begin() }; }
    const_reverse_iterator rend  () const noexcept { return const_reverse_iterator{ begin() }; }

    const_reverse_iterator crbegin() const noexcept { return rbegin(); }
    const_reverse_iterator crend  () const noexcept { return rend  (); }

    //--------------------------------------------------------------------------
    // Capacity
    //--------------------------------------------------------------------------
    [[nodiscard]] bool      empty   () const noexcept { return base::keys.empty(); }
    [[nodiscard]] size_type size    () const noexcept { return static_cast<size_type>( base::keys.size() ); }
    [[nodiscard]] size_type max_size() const noexcept { return std::min( base::keys.max_size(), base::values.max_size() ); }

    //--------------------------------------------------------------------------
    // Element access
    //--------------------------------------------------------------------------
    mapped_type & operator[]( key_type const & key ) {
        return try_emplace( key ).first->second;
    }

    mapped_type & operator[]( key_type && key ) {
        return try_emplace( std::move( key ) ).first->second;
    }

    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    mapped_type & operator[]( K && key ) {
        return try_emplace( std::forward<K>( key ) ).first->second;
    }

    mapped_type       & at( key_type const & key )       { auto const it{ find( key ) }; if ( it == end() ) detail::throw_out_of_range( "psi::vm::flat_map::at" ); return it->second; }
    mapped_type const & at( key_type const & key ) const { auto const it{ find( key ) }; if ( it == end() ) detail::throw_out_of_range( "psi::vm::flat_map::at" ); return it->second; }

    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    mapped_type       & at( K const & key )       { auto const it{ find( key ) }; if ( it == end() ) detail::throw_out_of_range( "psi::vm::flat_map::at" ); return it->second; }
    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    mapped_type const & at( K const & key ) const { auto const it{ find( key ) }; if ( it == end() ) detail::throw_out_of_range( "psi::vm::flat_map::at" ); return it->second; }

    //--------------------------------------------------------------------------
    // Lookup
    //--------------------------------------------------------------------------
private:
    template <typename K>
    [[nodiscard]] size_type lower_bound_index( K const & key ) const noexcept {
        auto const comp{ make_trivially_copyable_predicate( comp_ ) };
        return static_cast<size_type>( std::lower_bound( base::keys.begin(), base::keys.end(), key, comp ) - base::keys.begin() );
    }
    template <typename K>
    [[nodiscard]] size_type upper_bound_index( K const & key ) const noexcept {
        auto const comp{ make_trivially_copyable_predicate( comp_ ) };
        return static_cast<size_type>( std::upper_bound( base::keys.begin(), base::keys.end(), key, comp ) - base::keys.begin() );
    }
    template <typename K>
    [[nodiscard]] bool key_eq_at( size_type const pos, K const & key ) const noexcept {
        return pos < base::keys.size() && !comp_( key, base::keys[ pos ] );
    }

public:
    iterator       find( key_type const & key )       noexcept { auto const pos{ lower_bound_index( key ) }; return key_eq_at( pos, key ) ? iterator      { this, static_cast<difference_type>( pos ) } : end(); }
    const_iterator find( key_type const & key ) const noexcept { auto const pos{ lower_bound_index( key ) }; return key_eq_at( pos, key ) ? const_iterator{ this, static_cast<difference_type>( pos ) } : end(); }

    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    iterator       find( K const & key )       noexcept { auto const pos{ lower_bound_index( key ) }; return key_eq_at( pos, key ) ? iterator      { this, static_cast<difference_type>( pos ) } : end(); }
    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    const_iterator find( K const & key ) const noexcept { auto const pos{ lower_bound_index( key ) }; return key_eq_at( pos, key ) ? const_iterator{ this, static_cast<difference_type>( pos ) } : end(); }

    [[nodiscard]] size_type count   ( key_type const & key ) const noexcept { return find( key ) != end() ? 1 : 0; }
    [[nodiscard]] bool      contains( key_type const & key ) const noexcept { return find( key ) != end(); }

    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    [[nodiscard]] size_type count   ( K const & key ) const noexcept { return find( key ) != end() ? 1 : 0; }
    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    [[nodiscard]] bool      contains( K const & key ) const noexcept { return find( key ) != end(); }

    iterator       lower_bound( key_type const & key )       noexcept { return { this, static_cast<difference_type>( lower_bound_index( key ) ) }; }
    const_iterator lower_bound( key_type const & key ) const noexcept { return { this, static_cast<difference_type>( lower_bound_index( key ) ) }; }
    iterator       upper_bound( key_type const & key )       noexcept { return { this, static_cast<difference_type>( upper_bound_index( key ) ) }; }
    const_iterator upper_bound( key_type const & key ) const noexcept { return { this, static_cast<difference_type>( upper_bound_index( key ) ) }; }

    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    iterator       lower_bound( K const & key )       noexcept { return { this, static_cast<difference_type>( lower_bound_index( key ) ) }; }
    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    const_iterator lower_bound( K const & key ) const noexcept { return { this, static_cast<difference_type>( lower_bound_index( key ) ) }; }
    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    iterator       upper_bound( K const & key )       noexcept { return { this, static_cast<difference_type>( upper_bound_index( key ) ) }; }
    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    const_iterator upper_bound( K const & key ) const noexcept { return { this, static_cast<difference_type>( upper_bound_index( key ) ) }; }

    std::pair<iterator, iterator>             equal_range( key_type const & key )       noexcept { return { lower_bound( key ), upper_bound( key ) }; }
    std::pair<const_iterator, const_iterator> equal_range( key_type const & key ) const noexcept { return { lower_bound( key ), upper_bound( key ) }; }

    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    std::pair<iterator, iterator>             equal_range( K const & key )       noexcept { return { lower_bound( key ), upper_bound( key ) }; }
    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    std::pair<const_iterator, const_iterator> equal_range( K const & key ) const noexcept { return { lower_bound( key ), upper_bound( key ) }; }

    //--------------------------------------------------------------------------
    // Modifiers
    //--------------------------------------------------------------------------
    std::pair<iterator, bool> insert( value_type const & v ) {
        return emplace( v.first, v.second );
    }

    std::pair<iterator, bool> insert( value_type && v ) {
        return emplace( std::move( v.first ), std::move( v.second ) );
    }

    iterator insert( const_iterator hint, value_type const & v ) {
        return emplace_hint( hint, v.first, v.second );
    }

    iterator insert( const_iterator hint, value_type && v ) {
        return emplace_hint( hint, std::move( v.first ), std::move( v.second ) );
    }

    // Bulk insert — append + sort + merge + deduplicate
    template <std::input_iterator InputIt>
    void insert( InputIt first, InputIt last ) {
        auto const oldSize{ size() };
        base::append_from( first, last );
        sort_merge_unique<false>( oldSize );
    }

    void insert( std::initializer_list<value_type> il ) {
        insert( il.begin(), il.end() );
    }

    // Sorted unique bulk insert — append + merge + deduplicate (no sort)
    template <std::input_iterator InputIt>
    void insert( sorted_unique_t, InputIt first, InputIt last ) {
        auto const oldSize{ size() };
        base::append_from( first, last );
        sort_merge_unique<true>( oldSize );
    }

    void insert( sorted_unique_t, std::initializer_list<value_type> il ) {
        insert( sorted_unique, il.begin(), il.end() );
    }

    // insert_range — delegates to iterator pair insert via views::common
    template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, value_type>
    void insert_range( R && rg ) {
        if constexpr ( std::ranges::sized_range<R> )
            reserve( size() + static_cast<size_type>( std::ranges::size( rg ) ) );
        auto common{ std::forward<R>( rg ) | std::views::common };
        insert( std::ranges::begin( common ), std::ranges::end( common ) );
    }

    template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, value_type>
    void insert_range( sorted_unique_t, R && rg ) {
        if constexpr ( std::ranges::sized_range<R> )
            reserve( size() + static_cast<size_type>( std::ranges::size( rg ) ) );
        auto common{ std::forward<R>( rg ) | std::views::common };
        insert( sorted_unique, std::ranges::begin( common ), std::ranges::end( common ) );
    }

    template <typename K, typename... Args>
    std::pair<iterator, bool> try_emplace( K && key, Args &&... args ) {
        auto const pos{ lower_bound_index( key ) };
        if ( key_eq_at( pos, key ) )
            return { iterator{ this, static_cast<difference_type>( pos ) }, false };
        base::insert_element_at( pos, key_type( std::forward<K>( key ) ), mapped_type( std::forward<Args>( args )... ) );
        return { iterator{ this, static_cast<difference_type>( pos ) }, true };
    }

    template <typename K, typename... Args>
    iterator try_emplace( const_iterator hint, K && key, Args &&... args ) {
        auto const hintIdx{ static_cast<size_type>( hint.idx_ ) };
        bool const hintValid{
            ( hintIdx == 0      || comp_( base::keys[ hintIdx - 1 ], key ) ) &&
            ( hintIdx >= size() || comp_( key, base::keys[ hintIdx ] ) )
        };
        if ( hintValid ) {
            base::insert_element_at( hintIdx, key_type( std::forward<K>( key ) ), mapped_type( std::forward<Args>( args )... ) );
            return { this, static_cast<difference_type>( hintIdx ) };
        }
        if ( hintIdx < size() && !comp_( key, base::keys[ hintIdx ] ) && !comp_( base::keys[ hintIdx ], key ) )
            return { this, static_cast<difference_type>( hintIdx ) };
        return try_emplace( std::forward<K>( key ), std::forward<Args>( args )... ).first;
    }

    template <typename K, typename M>
    std::pair<iterator, bool> insert_or_assign( K && key, M && value ) {
        auto const pos{ lower_bound_index( key ) };
        if ( key_eq_at( pos, key ) ) {
            base::values[ pos ] = std::forward<M>( value );
            return { iterator{ this, static_cast<difference_type>( pos ) }, false };
        }
        base::insert_element_at( pos, key_type( std::forward<K>( key ) ), std::forward<M>( value ) );
        return { iterator{ this, static_cast<difference_type>( pos ) }, true };
    }

    template <typename K, typename M>
    iterator insert_or_assign( const_iterator hint, K && key, M && value ) {
        auto const hintIdx{ static_cast<size_type>( hint.idx_ ) };
        if ( hintIdx < size() && !comp_( key, base::keys[ hintIdx ] ) && !comp_( base::keys[ hintIdx ], key ) ) {
            base::values[ hintIdx ] = std::forward<M>( value );
            return { this, hint.idx_ };
        }
        return insert_or_assign( std::forward<K>( key ), std::forward<M>( value ) ).first;
    }

    template <typename... Args>
    std::pair<iterator, bool> emplace( Args &&... args ) {
        value_type v( std::forward<Args>( args )... );
        return try_emplace( std::move( v.first ), std::move( v.second ) );
    }

    iterator emplace_hint( const_iterator hint, auto &&... args ) {
        value_type v( std::forward<decltype( args )>( args )... );
        return try_emplace( hint, std::move( v.first ), std::move( v.second ) );
    }

    iterator erase( iterator pos ) noexcept { return erase( const_iterator{ pos } ); }

    iterator erase( const_iterator pos ) noexcept {
        auto const idx{ static_cast<size_type>( pos.idx_ ) };
        base::erase_element_at( idx );
        return { this, static_cast<difference_type>( idx ) };
    }

    iterator erase( const_iterator first, const_iterator last ) noexcept {
        auto const firstIdx{ static_cast<size_type>( first.idx_ ) };
        auto const lastIdx { static_cast<size_type>( last .idx_ ) };
        base::erase_elements( firstIdx, lastIdx );
        return { this, first.idx_ };
    }

    size_type erase( key_type const & key ) noexcept {
        auto const it{ find( key ) };
        if ( it == end() ) return 0;
        erase( const_iterator{ it } );
        return 1;
    }

    template <typename K> requires( requires{ typename Compare::is_transparent; } )
    size_type erase( K const & key ) noexcept {
        auto const it{ find( key ) };
        if ( it == end() ) return 0;
        erase( const_iterator{ it } );
        return 1;
    }

    using base::clear;

    void swap( flat_map & other ) noexcept {
        base::swap_storage( other );
        using std::swap;
        swap( comp_, other.comp_ );
    }

    friend void swap( flat_map & a, flat_map & b ) noexcept { a.swap( b ); }

    //--------------------------------------------------------------------------
    // Extraction & replacement
    //--------------------------------------------------------------------------
    containers extract() noexcept( std::is_nothrow_move_constructible_v<base> ) { return std::move( static_cast<base &>( *this ) ); }

    using base::replace;

    //--------------------------------------------------------------------------
    // Observers
    //--------------------------------------------------------------------------
    [[nodiscard]] key_compare   key_comp  () const noexcept { return comp_; }
    [[nodiscard]] value_compare value_comp() const noexcept { return value_compare{ comp_ }; }

    [[nodiscard]] key_container_type    const & keys  () const noexcept { return base::keys;   }
    [[nodiscard]] mapped_container_type const & values() const noexcept { return base::values; }

    //--------------------------------------------------------------------------
    // Merge (extension — not in std::flat_map, matches std::map::merge semantics)
    //--------------------------------------------------------------------------
    void merge( flat_map & source ) {
        if ( source.empty() || this == &source )
            return;
        // O(N+M) sorted scan to find source indices to transfer
        std::vector<size_type> transferIndices;
        transferIndices.reserve( source.size() );
        {
            size_type si{ 0 };
            size_type ti{ 0 };
            auto const sn{ source.size() };
            auto const tn{ size() };
            while ( si < sn && ti < tn ) {
                if ( comp_( source.base::keys[ si ], base::keys[ ti ] ) ) {
                    transferIndices.push_back( si );
                    ++si;
                } else if ( comp_( base::keys[ ti ], source.base::keys[ si ] ) ) {
                    ++ti;
                } else {
                    ++si;
                    ++ti;
                }
            }
            for ( ; si < sn; ++si )
                transferIndices.push_back( si );
        }
        if ( transferIndices.empty() )
            return;

        // Append only non-duplicate elements (already in sorted order)
        auto const oldSize{ size() };
        for ( auto const idx : transferIndices ) {
            base::keys  .emplace_back( std::move( source.base::keys  [ idx ] ) );
            base::values.emplace_back( std::move( source.base::values[ idx ] ) );
        }
        sort_merge_unique<true>( oldSize );
        // Compact source: remove transferred elements in O(M) single pass
        {
            size_type dst{ 0 };
            size_type nextT{ 0 };
            for ( size_type src{ 0 }, n{ source.size() }; src < n; ++src ) {
                if ( nextT < transferIndices.size() && src == transferIndices[ nextT ] ) {
                    ++nextT;
                } else {
                    if ( dst != src ) {
                        source.base::keys  [ dst ] = std::move( source.base::keys  [ src ] );
                        source.base::values[ dst ] = std::move( source.base::values[ src ] );
                    }
                    ++dst;
                }
            }
            source.truncate_to( dst );
        }
    }

    void merge( flat_map && source ) {
        auto const oldSize{ size() };
        base::append_move_containers( source.base::keys, source.base::values );
        sort_merge_unique<true>( oldSize );
        source.clear();
    }

    //--------------------------------------------------------------------------
    // Comparison
    //--------------------------------------------------------------------------
    friend bool operator==( flat_map const & a, flat_map const & b ) {
        return a.keys() == b.keys() && a.values() == b.values();
    }

    friend auto operator<=>( flat_map const & a, flat_map const & b )
    requires std::three_way_comparable<Key> && std::three_way_comparable<T>
    {
        if ( auto const cmp{ std::lexicographical_compare_three_way( a.keys().begin(), a.keys().end(), b.keys().begin(), b.keys().end() ) }; cmp != 0 )
            return cmp;
        return std::lexicographical_compare_three_way( a.values().begin(), a.values().end(), b.values().begin(), b.values().end() );
    }

    //--------------------------------------------------------------------------
    // erase_if (friend non-member)
    //--------------------------------------------------------------------------
    template <typename Pred>
    friend size_type erase_if( flat_map & c, Pred pred ) {
        auto zv{ c.zip_view() };
        auto const it{ std::remove_if( zv.begin(), zv.end(), [&pred]( auto && zipped ) {
            return pred( const_reference{ std::get<0>( zipped ), std::get<1>( zipped ) } );
        } ) };
        auto const newSize{ static_cast<size_type>( it - zv.begin() ) };
        auto const erased { static_cast<size_type>( zv.end() - it ) };
        c.truncate_to( newSize );
        return erased;
    }

    //--------------------------------------------------------------------------
    // Reserve (extension — not in std but useful)
    //--------------------------------------------------------------------------
    using base::reserve;
    using base::shrink_to_fit;

    //--------------------------------------------------------------------------
    // Private helpers
    //--------------------------------------------------------------------------
private:
    // Sort entire map and remove duplicates (first occurrence kept)
    void sort_and_unique() {
        auto zv{ base::zip_view() };
        std::ranges::sort( zv, comp_, key_proj() );
        auto const dupStart{ std::ranges::unique( zv, key_equiv(), key_proj() ).begin() };
        base::truncate_to( static_cast<size_type>( dupStart - zv.begin() ) );
    }

    // Bulk insert: sort new tail, inplace_merge with existing, deduplicate
    template <bool WasSorted>
    void sort_merge_unique( size_type const oldSize ) {
        if ( static_cast<size_type>( base::keys.size() ) <= oldSize )
            return;

        auto zv{ base::zip_view() };
        auto const appendStart{ zv.begin() + static_cast<difference_type>( oldSize ) };

        if constexpr ( !WasSorted )
            std::ranges::sort( appendStart, zv.end(), comp_, key_proj() );

        if ( oldSize > 0 )
            std::ranges::inplace_merge( zv.begin(), appendStart, zv.end(), comp_, key_proj() );

        auto const dupStart{ std::ranges::unique( zv, key_equiv(), key_proj() ).begin() };
        base::truncate_to( static_cast<size_type>( dupStart - zv.begin() ) );
    }

    // Key projection for ranges algorithms (sort, inplace_merge, unique)
    // key_proj() is a range adaptor, not a per-element callable — need a custom functor
    static constexpr auto key_proj() noexcept {
        return []<typename E>( E && e ) -> decltype(auto) {
            return std::get<0>( std::forward<E>( e ) );
        };
    }

    // Key equivalence predicate for ranges::unique (receives projected keys)
    auto key_equiv() const noexcept {
        return [this]( auto const & a, auto const & b ) {
            return !comp_( a, b ) && !comp_( b, a );
        };
    }

    //--------------------------------------------------------------------------
    // Data members
    //--------------------------------------------------------------------------
#ifdef _MSC_VER
    [[ msvc::no_unique_address ]]
#else
    [[ no_unique_address ]]
#endif
    key_compare comp_;
}; // class flat_map

//------------------------------------------------------------------------------
// Deduction guides
//------------------------------------------------------------------------------

template <typename KC, typename MC, typename Comp = std::less<typename KC::value_type>>
requires( !std::is_same_v<KC, sorted_unique_t> )
flat_map( KC, MC, Comp = Comp{} )
    -> flat_map<typename KC::value_type, typename MC::value_type, Comp, KC, MC>;

template <typename KC, typename MC, typename Comp = std::less<typename KC::value_type>>
flat_map( sorted_unique_t, KC, MC, Comp = Comp{} )
    -> flat_map<typename KC::value_type, typename MC::value_type, Comp, KC, MC>;

template <std::input_iterator InputIt, typename Comp = std::less<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type::first_type>>>
requires( !std::is_same_v<InputIt, sorted_unique_t> )
flat_map( InputIt, InputIt, Comp = Comp{} )
    -> flat_map<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type::first_type>,
                typename std::iterator_traits<InputIt>::value_type::second_type,
                Comp>;

template <std::input_iterator InputIt, typename Comp = std::less<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type::first_type>>>
flat_map( sorted_unique_t, InputIt, InputIt, Comp = Comp{} )
    -> flat_map<std::remove_const_t<typename std::iterator_traits<InputIt>::value_type::first_type>,
                typename std::iterator_traits<InputIt>::value_type::second_type,
                Comp>;

template <std::ranges::input_range R, typename Comp = std::less<std::remove_const_t<typename std::ranges::range_value_t<R>::first_type>>>
flat_map( std::from_range_t, R &&, Comp = Comp{} )
    -> flat_map<std::remove_const_t<typename std::ranges::range_value_t<R>::first_type>,
                typename std::ranges::range_value_t<R>::second_type,
                Comp>;

template <typename Key, typename T, typename Comp = std::less<Key>>
flat_map( std::initializer_list<std::pair<Key, T>>, Comp = Comp{} )
    -> flat_map<Key, T, Comp>;

template <typename Key, typename T, typename Comp = std::less<Key>>
flat_map( sorted_unique_t, std::initializer_list<std::pair<Key, T>>, Comp = Comp{} )
    -> flat_map<Key, T, Comp>;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
