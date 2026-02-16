////////////////////////////////////////////////////////////////////////////////
/// Shared foundations for psi::vm flat sorted containers (flat_map, flat_set,
/// flat_multimap, flat_multiset).
///
/// Contents:
///   - sorted_unique_t, sorted_equivalent_t  (sorted-input hint tags)
///   - detail lookup utilities               (lower_bound_iter, upper_bound_iter, ...)
///   - detail sort/dedup utilities           (unique_truncate, inplace_set_unique_difference, ...)
///   - detail storage abstraction helpers    (keys_of, storage_erase_at, ...)
///   - flat_impl<Storage, Compare>           (shared base for flat_set/flat_map families)
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

#include "komparator.hpp"
#include "lookup.hpp"

#include <boost/assert.hpp>

#if __has_include( <boost/move/algo/adaptive_merge.hpp> )
#include <boost/move/algo/adaptive_merge.hpp>
#define PSI_VM_HAS_ADAPTIVE_MERGE 1
#else
#define PSI_VM_HAS_ADAPTIVE_MERGE 0
#endif

#include <algorithm>
#include <compare>
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
// Sorted-input hint tags
//==============================================================================
struct sorted_unique_t     { explicit sorted_unique_t    () = default; };
struct sorted_equivalent_t { explicit sorted_equivalent_t() = default; };

inline constexpr sorted_unique_t     sorted_unique    {};
inline constexpr sorted_equivalent_t sorted_equivalent{};

template <typename T>
concept sorted_insert_tag = ::std::same_as<T, sorted_unique_t> || ::std::same_as<T, sorted_equivalent_t>;


namespace detail {
    //==============================================================================
    // truncate_to — shrink a single container to n elements (baseline primitive)
    //
    // paired_storage::truncate_to delegates to this for each sub-container.
    //==============================================================================
    template <typename C>
    constexpr void truncate_to( C & c, typename C::size_type const n ) noexcept {
        if constexpr ( requires { c.shrink_to( n ); } )
            c.shrink_to( n );
        else
            c.resize( n );
    }

    //==============================================================================
    // Lookup utilities (shared by flat_map and flat_set families)
    //==============================================================================

    // Lean worker functions — comparator passed in registers; key accepts
    // both Reg-wrapped and plain const-ref types (the compiler optimizes away
    // the indirection for inlined Reg types; non-transparent + non-trivial
    // key_type arrives as key_type const & which is not Reg).

    [[nodiscard, gnu::sysv_abi, gnu::pure]] constexpr
    auto lower_bound_iter( auto const & keys, Reg auto const comparator, Reg auto const key ) noexcept {
        decltype( auto ) comp { unwrap  ( comparator ) };
        decltype( auto ) value{ prefetch( comp, key ) };
        return std::lower_bound( keys.begin(), keys.end(), value, make_trivially_copyable_predicate( comp ) );
    }

    [[nodiscard, gnu::sysv_abi, gnu::pure]] constexpr
    auto upper_bound_iter( auto const & keys, Reg auto const comparator, Reg auto const key ) noexcept {
        decltype( auto ) comp { unwrap  ( comparator ) };
        decltype( auto ) value{ prefetch( comp, key ) };
        return std::upper_bound( keys.begin(), keys.end(), value, make_trivially_copyable_predicate( comp ) );
    }

    [[nodiscard, gnu::sysv_abi, gnu::pure]] constexpr
    auto equal_range_iter( auto const & keys, Reg auto const comparator, Reg auto const key ) noexcept {
        decltype( auto ) comp { unwrap  ( comparator ) };
        decltype( auto ) value{ prefetch( comp, key ) };
        auto const wrappedComp{ make_trivially_copyable_predicate( comp ) };
        auto const lb{ std::lower_bound( keys.begin(), keys.end(), value, wrappedComp ) };
        // upper_bound search starts from lb — no redundant traversal of [begin, lb)
        auto const ub{ std::upper_bound( lb, keys.end(), value, wrappedComp ) };
        return std::pair{ lb, ub };
    }

    // Key equivalence predicate from a strict-weak comparator (uses comp_eq for
    // optimized equality where possible, e.g. == for simple comparators)
    template <typename Comp>
    [[nodiscard]] constexpr auto key_equiv( Comp const & comp ) noexcept {
        return [&comp]( auto const & a, auto const & b ) {
            return comp_eq( comp, a, b );
        };
    }

    // Key projection for zip views (extracts first element from tuple)
    constexpr auto key_proj() noexcept {
        return []<typename E>( E && e ) -> decltype(auto) {
            return std::get<0>( std::forward<E>( e ) );
        };
    }


    //==============================================================================
    // Merge/dedup strategy flags — constexpr switches for experimentation.
    // Only affect the single-container (set) overload of sort_merge_storage.
    //==============================================================================
    inline constexpr bool use_set_difference_dedup{ true  };  // filter tail against prefix before merge (vs. merge-then-unique)
    // adaptive_merge uses spare capacity past size() as scratch buffer which
    // trips ASan's container-overflow detection (writes between size/capacity).
#if defined( __SANITIZE_ADDRESS__ )
    inline constexpr bool use_adaptive_merge      { false };
#elif defined( __has_feature )
#   if __has_feature( address_sanitizer )
    inline constexpr bool use_adaptive_merge      { false };
#   else
    inline constexpr bool use_adaptive_merge      { true  };  // boost::movelib::adaptive_merge (vs. std::inplace_merge)
#   endif
#else
    inline constexpr bool use_adaptive_merge      { true  };  // boost::movelib::adaptive_merge (vs. std::inplace_merge)
#endif


    //==============================================================================
    // set_unique_difference — algorithms for pre-merge duplicate filtering
    //
    // Ported from boost/move/algo/detail/set_difference.hpp using std::move
    // instead of boost::move.
    //==============================================================================

    /// Non-inplace variant: moves elements from sorted [first1, last1) that are
    /// not found in sorted [first2, last2) to `result`, also removing adjacent
    /// duplicates from range1.  Returns output iterator past last written.
    template <typename FwdIt1, typename InputIt2, typename OutputIt>
    OutputIt set_unique_difference( FwdIt1 first1, FwdIt1 last1, InputIt2 first2, InputIt2 last2, OutputIt result, Reg auto const comparator ) noexcept
    {
        auto const & comp{ unwrap( comparator ) };
        while ( first1 != last1 ) {
            if ( first2 == last2 ) {
                // unique-copy remainder of range1
                FwdIt1 i{ first1 };
                while ( ++first1 != last1 ) {
                    if ( comp( *i, *first1 ) ) {
                        *result = std::move( *i );
                        ++result;
                        i = first1;
                    }
                }
                *result = std::move( *i );
                ++result;
                break;
            }

            if ( comp( *first1, *first2 ) ) {
                // *first1 < *first2 — skip adjacent equivalents in range1
                FwdIt1 i{ first1 };
                while ( ++first1 != last1 ) {
                    if ( comp( *i, *first1 ) )
                        break;
                }
                *result = std::move( *i );
                ++result;
            } else if ( comp( *first2, *first1 ) ) {
                ++first2;
            } else {
                // equivalent — skip from range1
                ++first1;
            }
        }
        return result;
    }

    /// In-place variant: compacts sorted [first1, last1) by removing elements
    /// found in sorted [first2, last2) and adjacent duplicates.
    /// Returns iterator to new end of range1.
    /// Falls back to set_unique_difference with move_iterators when compaction
    /// actually starts moving elements.
    template <typename FwdOutIt, typename FwdIt2>
    FwdOutIt inplace_set_unique_difference( FwdOutIt first1, FwdOutIt last1, FwdIt2 first2, FwdIt2 last2, Reg auto const comparator ) noexcept
    {
        auto const & comp{ unwrap( comparator ) };
        while ( first1 != last1 ) {
            if ( first2 == last2 ) {
                // unique-in-place for remainder of range1
                FwdOutIt result{ first1 };
                while ( ++first1 != last1 ) {
                    if ( comp( *result, *first1 ) && ++result != first1 )
                        *result = std::move( *first1 );
                }
                return ++result;
            }
            if ( comp( *first2, *first1 ) ) {
                ++first2;
            } else if ( comp( *first1, *first2 ) ) {
                // *first1 < *first2 — check for adjacent duplicates in range1
                FwdOutIt result{ first1 };
                if ( ++first1 != last1 && !comp( *result, *first1 ) ) {
                    // adjacent dups found — switch to non-inplace
                    while ( ++first1 != last1 && !comp( *result, *first1 ) )
                        {}
                    return set_unique_difference(
                        std::move_iterator{ first1 }, std::move_iterator{ last1 },
                        first2, last2, ++result, comparator
                    );
                }
            } else {
                // equivalent to an element in range2 — must skip
                FwdOutIt result{ first1 };
                while ( ++first1 != last1 && !comp( *result, *first1 ) )
                    {}
                return set_unique_difference(
                    std::move_iterator{ first1 }, std::move_iterator{ last1 },
                    first2, last2, result, comparator
                );
            }
        }
        return first1;
    }


    //==============================================================================
    // do_inplace_merge — merge helper, chooses between std::inplace_merge and
    // boost::movelib::adaptive_merge (which uses spare capacity as scratch buffer)
    //==============================================================================
    template <typename KC>
    constexpr void do_inplace_merge( KC & keys, typename KC::size_type const oldSize, auto const & comp ) noexcept {
        auto const wrappedComp{ make_trivially_copyable_predicate( comp ) };
        auto const middle{ keys.begin() + static_cast<std::ptrdiff_t>( oldSize ) };
    #if PSI_VM_HAS_ADAPTIVE_MERGE
        if constexpr ( use_adaptive_merge )
        {
            // Use spare capacity past size() as uninitialized scratch buffer
            auto * const buffer { keys.data() + keys.size() };
            auto   const bufLen { keys.capacity() - keys.size() };
            boost::movelib::adaptive_merge(
                keys.data(),
                keys.data() + static_cast<std::ptrdiff_t>( oldSize ),
                keys.data() + static_cast<std::ptrdiff_t>( keys.size() ),
                wrappedComp, buffer, bufLen );
        } else
    #endif
        {
            std::inplace_merge( keys.begin(), middle, keys.end(), wrappedComp);
        }
    }


    //==============================================================================
    // Storage abstraction helpers — generic (set-path) overloads for single
    // containers.  paired_storage (map-path) overloads live in flat_map.hpp
    // and are found via ADL at template instantiation time.
    //==============================================================================

    // keys_of — extract the keys container from storage
    template <typename KC> constexpr auto       & keys_of( KC       & c ) noexcept { return c; }
    template <typename KC> constexpr auto const & keys_of( KC const & c ) noexcept { return c; }


    //==============================================================================
    // key_type_of — extracts key type from Storage
    // (KeyContainer for sets, paired_storage for maps)
    //==============================================================================

    template <typename Storage>
    using key_type_of = std::ranges::range_value_t<std::remove_cvref_t<decltype( keys_of( std::declval<Storage const &>() ) )>>;


    // unique_truncate — dedup in place and shrink; common tail for sort paths
    template <typename KC, typename Comp>
    constexpr void unique_truncate( KC & keys, Comp const & comp ) noexcept {
        auto const newEnd{ std::ranges::unique( keys, key_equiv( comp ) ).begin() };
        truncate_to( keys, static_cast<typename KC::size_type>( newEnd - keys.begin() ) );
    }

    // sort_storage — sort + conditional dedup, truncating storage in place
    // Single container (set): Komp is Komparator — .sort() dispatches to
    // pdqsort_branchless when available; .comp() yields the raw comparator
    // for STL functions (fewer template instantiations).
    template <bool Unique, typename KC, typename Komp>
    constexpr void sort_storage( KC & keys, Komp const & komp ) {
        komp.sort( keys.begin(), keys.end() );
        if constexpr ( Unique )
            unique_truncate( keys, komp.comp() );
    }

    // sort_merge_storage — sort appended tail, merge with sorted prefix, conditional dedup
    template <bool Unique, bool WasSorted, typename KC, typename Komp>
    constexpr void sort_merge_storage( KC & keys, Komp const & komp, typename KC::size_type const oldSize ) {
        using key_sz_t = typename KC::size_type;
        if ( keys.size() <= oldSize )
            return;
        auto const appendStart{ keys.begin() + static_cast<std::ptrdiff_t>( oldSize ) };
        auto const & comp{ komp.comp() };

        if constexpr ( !WasSorted )
            komp.sort( appendStart, keys.end() );

        if constexpr ( Unique && use_set_difference_dedup ) {
            // Filter tail against prefix: removes (a) elements already in prefix,
            // (b) adjacent duplicates within tail. Result: tail is unique and
            // disjoint from prefix, so no final unique pass needed after merge.
            if ( oldSize > 0 ) {
                auto const newTailEnd{ inplace_set_unique_difference( appendStart, keys.end(), keys.begin(), appendStart, enreg( comp ) ) };
                truncate_to( keys, static_cast<key_sz_t>( newTailEnd - keys.begin() ) );
                if ( static_cast<key_sz_t>( keys.size() ) > oldSize )
                    do_inplace_merge( keys, oldSize, comp );
            } else {
                unique_truncate( keys, comp );
            }
        } else {
            if ( oldSize > 0 )
                do_inplace_merge( keys, oldSize, comp );
            if constexpr ( Unique )
                unique_truncate( keys, comp );
        }
    }

    // storage_erase_at — erase single element at position
    template <typename KC>
    constexpr void storage_erase_at( KC & c, typename KC::size_type const pos ) noexcept {
        c.erase( c.begin() + static_cast<std::ptrdiff_t>( pos ) );
    }

    // storage_erase_range — erase elements in [first, last)
    template <typename KC>
    constexpr void storage_erase_range( KC & c, typename KC::size_type const first, typename KC::size_type const last ) noexcept {
        c.erase( c.begin() + static_cast<std::ptrdiff_t>( first ), c.begin() + static_cast<std::ptrdiff_t>( last ) );
    }

    // storage_move_append — move all elements from source into dest
    template <typename KC>
    constexpr void storage_move_append( KC & dest, KC & source ) {
        dest.append_range( source | std::views::as_rvalue );
    }

    // storage_emplace_back_from — move single element at source[idx] to back of dest
    template <typename KC>
    constexpr void storage_emplace_back_from( KC & dest, KC & source, typename KC::size_type const idx ) {
        dest.emplace_back( std::move( source[ idx ] ) );
    }

    // storage_emplace_back — unchecked append (set path: single container)
    template <typename KC, typename... Args>
    constexpr void storage_emplace_back( KC & c, Args &&... args ) {
        c.emplace_back( std::forward<Args>( args )... );
    }

    // storage_back — reference to the last emplaced element
    template <typename KC>
    constexpr auto & storage_back( KC & c ) noexcept { return c.back(); }

    // storage_move_element — move element from src to dst position within same storage
    template <typename KC>
    constexpr void storage_move_element( KC & c, typename KC::size_type const dst, typename KC::size_type const src ) noexcept {
        c[ dst ] = std::move( c[ src ] );
    }

//==============================================================================
// flat_impl — shared base for flat_set_impl and flat_map_impl
//
// Lives in namespace detail so that unqualified calls to  functions
// (keys_of, storage_erase_at, ...) resolve via ordinary lookup for the
// set-path overloads, while map-path overloads (in flat_map.hpp) are found
// via ADL on paired_storage at instantiation time.
//
// A `using detail::flat_impl;` in psi::vm makes the name available to
// the derived classes (flat_set_impl, flat_map_impl) in the outer namespace.
//
// Holds the storage and comparator, provides capacity, clear, reserve,
// key_comp, comparison, merge, lookup index helpers, and deducing-this
// sort utilities.
// Parameterised on Storage (KeyContainer for sets, paired_storage for maps).
//==============================================================================

template <typename Storage, typename Compare>
class flat_impl : protected Komparator<Compare>
{
    using Komp = Komparator<Compare>;

public:
    using Komparator<Compare>::transparent_comparator;
    using key_type        = key_type_of<Storage>;
    using value_type      = typename Storage::value_type;
    using key_compare     = Compare;
    using size_type       = typename Storage::size_type;
    using difference_type = std::ptrdiff_t;
    using key_const_arg   = key_const_arg_t<key_type, Komp::transparent_comparator>;

    static auto constexpr nothrow_move_constructible
    {
        std::is_nothrow_move_constructible_v<Storage> &&
        std::is_nothrow_copy_constructible_v<Compare>
    };

    //--------------------------------------------------------------------------
    // Derived iterators (require begin()/end() from derived class via deducing-this)
    //--------------------------------------------------------------------------
    constexpr auto cbegin( this auto const & self ) noexcept { return self.begin(); }
    constexpr auto cend  ( this auto const & self ) noexcept { return self.end();   }

    constexpr auto rbegin( this auto && self ) noexcept { return std::reverse_iterator{ self.end()   }; }
    constexpr auto rend  ( this auto && self ) noexcept { return std::reverse_iterator{ self.begin() }; }

    constexpr auto crbegin( this auto const & self ) noexcept { return std::reverse_iterator{ self.end()   }; }
    constexpr auto crend  ( this auto const & self ) noexcept { return std::reverse_iterator{ self.begin() }; }

    //--------------------------------------------------------------------------
    // Capacity
    //--------------------------------------------------------------------------
    [[nodiscard]] constexpr bool      empty() const noexcept { return keys_of( storage_ ).empty(); }
    [[nodiscard]] constexpr size_type size () const noexcept { return static_cast<size_type>( keys_of( storage_ ).size() ); }

    //--------------------------------------------------------------------------
    // Modifiers
    //--------------------------------------------------------------------------
    constexpr void clear() noexcept { storage_.clear(); }

    /// Replace contents from initializer list (clear + insert).
    constexpr void assign( this auto && self, std::initializer_list<value_type> il ) {
        self.clear();
        self.insert( il );
    }


    //--------------------------------------------------------------------------
    // Unchecked append (extension)
    //
    // Appends a key (set) or key-value pair (map) at the end without searching
    // for the insertion position.  Precondition: the key is greater than all
    // existing keys (asserted in debug builds).  This is the optimal path for
    // building a flat container from pre-sorted unique data.
    //--------------------------------------------------------------------------
    template <typename... Args>
    constexpr auto & emplace_back( this auto && self, key_type key, Args &&... args ) {
        BOOST_ASSERT( self.empty() || self.ge( key, self.keys().back() ) );
        storage_emplace_back( self.storage_, std::move( key ), std::forward<Args>( args )... );
        return storage_back( self.storage_ );
    }

    //--------------------------------------------------------------------------
    // Bulk insert — append + sort + merge (± dedup based on derived class's unique)
    //--------------------------------------------------------------------------
    template <std::input_iterator InputIt>
    constexpr void insert( this auto && self, InputIt first, InputIt last ) {
        self.template bulk_insert<false>( first, last );
    }

    constexpr void insert( this auto && self, std::initializer_list<value_type> il ) {
        self.insert( il.begin(), il.end() );
    }

    template <sorted_insert_tag SortedTag, std::input_iterator InputIt>
    constexpr void insert( this auto && self, SortedTag, InputIt first, InputIt last ) {
        self.template bulk_insert<true>( first, last );
    }

    template <sorted_insert_tag SortedTag>
    constexpr void insert( this auto && self, SortedTag s, std::initializer_list<value_type> il ) {
        self.insert( s, il.begin(), il.end() );
    }

    template <std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, value_type>
    constexpr void insert_range( this auto && self, R && rg ) {
        auto common{ std::forward<R>( rg ) | std::views::common };
        self.insert( std::ranges::begin( common ), std::ranges::end( common ) );
    }

    template <sorted_insert_tag SortedTag, std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, value_type>
    constexpr void insert_range( this auto && self, SortedTag s, R && rg ) {
        auto common{ std::forward<R>( rg ) | std::views::common };
        self.insert( s, std::ranges::begin( common ), std::ranges::end( common ) );
    }

    //--------------------------------------------------------------------------
    // Observers
    //--------------------------------------------------------------------------
    [[nodiscard]] constexpr key_compare   key_comp        () const noexcept { return this->comp(); }
    [[nodiscard]] constexpr key_compare & key_comp_mutable()       noexcept { return this->comp(); }

    //--------------------------------------------------------------------------
    // Extensions — reserve, shrink_to_fit
    //--------------------------------------------------------------------------
    constexpr void reserve( size_type const n ) { storage_.reserve( n ); }
    constexpr void shrink_to_fit() noexcept     { storage_.shrink_to_fit(); }

    //--------------------------------------------------------------------------
    // Comparison
    //--------------------------------------------------------------------------
    friend constexpr bool operator==( flat_impl const & a, flat_impl const & b ) noexcept {
        return a.storage_ == b.storage_;
    }

    friend constexpr auto operator<=>( flat_impl const & a, flat_impl const & b ) noexcept
    requires requires( Storage const & s ) { s <=> s; }
    {
        return a.storage_ <=> b.storage_;
    }

    //--------------------------------------------------------------------------
    // Merge (deducing-this: auto-detects unique vs multi from derived class)
    //--------------------------------------------------------------------------

    // Lvalue merge — unique: selective transfer of non-duplicate elements;
    //                multi:  move all elements from source.
    // Exception-safe: clears destination on failure (basic guarantee).
    constexpr void merge( this auto && self, flat_impl & source ) {
        if ( &self == &source )
            return;
        if constexpr ( is_unique<decltype( self )> ) {
            std::vector<size_type> transferIndices;
            transferIndices.reserve( source.size() );
            {
                auto const & srcKeys{ keys_of( source.storage_ ) };
                auto const & dstKeys{ keys_of( self  .storage_ ) };
                size_type si{ 0 };
                size_type ti{ 0 };
                auto const sn{ source.size() };
                auto const tn{ self  .size() };
                while ( si < sn && ti < tn ) {
                    if ( self.le( srcKeys[ si ], dstKeys[ ti ] ) ) {
                        transferIndices.push_back( si );
                        ++si;
                    } else if ( self.le( dstKeys[ ti ], srcKeys[ si ] ) ) {
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

            auto const oldSize{ self.size() };
            try {
                for ( auto const idx : transferIndices )
                    storage_emplace_back_from( self.storage_, source.storage_, idx );
                self.template init_sort_merge<true>( oldSize );
            } catch ( ... ) {
                self.clear();
                throw;
            }
            // Compact source — remove transferred elements
            {
                size_type dst{ 0 };
                size_type nextT{ 0 };
                for ( size_type src{ 0 }, n{ source.size() }; src < n; ++src ) {
                    if ( nextT < transferIndices.size() && src == transferIndices[ nextT ] ) {
                        ++nextT;
                    } else {
                        if ( dst != src )
                            storage_move_element( source.storage_, dst, src );
                        ++dst;
                    }
                }
                truncate_to( source.storage_, dst );
            }
        } else {
            self.merge( std::move( source ) ); // forward to rvalue merge (move all)
        }
    }

    // Rvalue merge — always move all (source is expiring)
    // Exception-safe: clears destination on failure (basic guarantee).
    constexpr void merge( this auto && self, flat_impl && source ) {
        auto const oldSize{ self.size() };
        storage_move_append( self.storage_, source.storage_ );
        try {
            self.template init_sort_merge<true>( oldSize );
        } catch ( ... ) {
            self.clear();
            throw;
        }
        source.clear();
    }

public:
    //--------------------------------------------------------------------------
    // Lookup — single constrained template per function
    //
    // Deducing-this resolves self to the derived type, so self.iter_from_key()
    // / self.begin() / self.end() produce the correct iterator type (set vs
    // map, const vs mutable).  Key-container iterators from lower_bound_keys
    // etc. are converted to the derived class's iterator via iter_from_key().
    //--------------------------------------------------------------------------
    template <LookupType<Komp::transparent_comparator, key_type> K = key_type>
    constexpr auto find( this auto && self, K const & key ) noexcept {
        auto const & keys  { keys_of( self.storage_ ) };
        auto const   key_it{ self.lower_bound_keys( enreg( key ) ) };
        if ( key_it != keys.end() && !self.comp()( key, *key_it ) )
            return self.iter_from_key( key_it );
        return self.end();
    }

    template <LookupType<Komp::transparent_comparator, key_type> K = key_type>
    [[nodiscard]] constexpr bool contains( this auto const & self, K const & key ) noexcept {
        return self.find( key ) != self.end();
    }

    template <LookupType<Komp::transparent_comparator, key_type> K = key_type>
    [[nodiscard]] constexpr size_type count( this auto const & self, K const & key ) noexcept {
        if constexpr ( is_unique<decltype( self )> ) {
            return self.contains( key );
        } else {
            auto const [lb, ub]{ self.equal_range_keys( enreg( key ) ) };
            return static_cast<size_type>( ub - lb );
        }
    }

    template <LookupType<Komp::transparent_comparator, key_type> K = key_type>
    constexpr auto lower_bound( this auto && self, K const & key ) noexcept {
        return self.iter_from_key( self.lower_bound_keys( enreg( key ) ) );
    }

    template <LookupType<Komp::transparent_comparator, key_type> K = key_type>
    constexpr auto upper_bound( this auto && self, K const & key ) noexcept {
        return self.iter_from_key( self.upper_bound_keys( enreg( key ) ) );
    }

    template <LookupType<Komp::transparent_comparator, key_type> K = key_type>
    constexpr auto equal_range( this auto && self, K const & key ) noexcept {
        auto const [lb, ub]{ self.equal_range_keys( enreg( key ) ) };
        return std::pair{ self.iter_from_key( lb ), self.iter_from_key( ub ) };
    }

    //--------------------------------------------------------------------------
    // Positional access
    //--------------------------------------------------------------------------
    constexpr auto nth( this auto && self, size_type const n ) noexcept {
        BOOST_ASSERT( n <= self.size() );
        return self.make_iter( n );
    }

    constexpr size_type index_of( this auto const & self, auto const it ) noexcept {
        return self.iter_index( it );
    }

    //--------------------------------------------------------------------------
    // Extraction & observers
    //--------------------------------------------------------------------------
    constexpr Storage extract() noexcept( std::is_nothrow_move_constructible_v<Storage> ) {
        return std::move( storage_ );
    }

    [[nodiscard]] constexpr auto const & keys() const noexcept {
        return keys_of( storage_ );
    }

protected:
    //--------------------------------------------------------------------------
    // erase_if (friend, found via ADL from any derived class)
    //
    // Delegates to erase_if(storage, pred):
    //   set path  → std::erase_if on the underlying container (via ADL)
    //   map path  → erase_if(paired_storage, pred) in flat_map.hpp
    //--------------------------------------------------------------------------
    template <typename Pred>
    friend constexpr size_type erase_if( flat_impl & c, Pred pred ) {
        return static_cast<size_type>( erase_if( c.storage_, std::move( pred ) ) );
    }


    [[ nodiscard ]] constexpr Komp const & komp() const noexcept { return *this; }

    constexpr flat_impl() = default;

    constexpr explicit flat_impl( Compare const & comp ) noexcept( std::is_nothrow_copy_constructible_v<Compare> )
        : Komp{ comp } {}

    constexpr flat_impl( Compare const & comp, Storage storage ) noexcept( nothrow_move_constructible )
        : Komp{ comp }, storage_{ std::move( storage ) } {}

    // "Fake deducing-this" constructors — Derived & gives access to
    // Derived::unique (static constexpr bool) during base construction.

    // Unsorted storage — sort (± dedup) in place
    template <typename Derived>
    constexpr flat_impl( Derived &, Compare const & comp, Storage storage )
        : Komp{ comp }, storage_{ std::move( storage ) }
    {
        sort_storage<Derived::unique>( storage_, this->komp() );
    }

    // Unsorted iterator pair — construct storage from [first, last), then sort
    template <typename Derived, std::input_iterator InputIt>
    constexpr flat_impl( Derived &, Compare const & comp, InputIt first, InputIt last )
        : Komp{ comp }, storage_{ first, last }
    {
        sort_storage<Derived::unique>( storage_, this->komp() );
    }

    // Pre-sorted iterator pair — no sorting needed
    template <std::input_iterator InputIt>
    constexpr flat_impl( Compare const & comp, InputIt first, InputIt last )
        : Komp{ comp }, storage_{ first, last }
    {}

    // from_range — append then sort
    template <typename Derived, std::ranges::input_range R>
    constexpr flat_impl( Derived &, Compare const & comp, std::from_range_t, R && rg )
        : Komp{ comp }
    {
        storage_.append_range( std::forward<R>( rg ) );
        sort_storage<Derived::unique>( storage_, this->komp() );
    }

    constexpr flat_impl( flat_impl const & ) = default;
    constexpr flat_impl( flat_impl && )      = default;
    constexpr flat_impl & operator=( flat_impl const & ) = default;
    constexpr flat_impl & operator=( flat_impl && )      = default;

    //--------------------------------------------------------------------------
    // Lookup helpers — key-container iterators (primary)
    // Keys arrive pre-wrapped (via enreg) from public API; the
    // detail:: workers accept Reg auto const so no further wrapping needed.
    //--------------------------------------------------------------------------
    [[nodiscard]] constexpr auto lower_bound_keys( Reg auto const key ) const noexcept {
        return lower_bound_iter( keys_of( storage_ ), enreg( this->comp() ), key );
    }
    [[nodiscard]] constexpr auto upper_bound_keys( Reg auto const key ) const noexcept {
        return upper_bound_iter( keys_of( storage_ ), enreg( this->comp() ), key );
    }
    [[nodiscard]] constexpr auto equal_range_keys( Reg auto const key ) const noexcept {
        return equal_range_iter( keys_of( storage_ ), enreg( this->comp() ), key );
    }
    //--------------------------------------------------------------------------
    // Erase-by-key worker — derived one-liners delegate here
    //--------------------------------------------------------------------------
    constexpr size_type erase_by_key_impl( Reg auto const key ) noexcept {
        auto const & keys{ keys_of( storage_ ) };
        auto const [lb, ub]{ equal_range_keys( key ) };
        auto const n{ static_cast<size_type>( ub - lb ) };
        if ( n ) {
            auto const firstIdx{ static_cast<size_type>( lb - keys.begin() ) };
            auto const lastIdx { static_cast<size_type>( ub - keys.begin() ) };
            storage_erase_range( storage_, firstIdx, lastIdx );
        }
        return n;
    }

    //--------------------------------------------------------------------------
    // Positional erase workers — deducing-this delegates to derived class's
    // public iter_index() / make_iter() for iterator ↔ index conversion.
    //--------------------------------------------------------------------------
    constexpr auto erase_pos_impl( this auto && self, auto pos ) noexcept {
        auto const idx{ self.iter_index( pos ) };
        storage_erase_at( self.storage_, idx );
        return self.make_iter( idx );
    }

    constexpr auto erase_range_impl( this auto && self, auto first, auto last ) noexcept {
        auto const firstIdx{ self.iter_index( first ) };
        auto const lastIdx { self.iter_index( last  ) };
        storage_erase_range( self.storage_, firstIdx, lastIdx );
        return self.make_iter( firstIdx );
    }

    //--------------------------------------------------------------------------
    // Lookup helpers — index-based (for emplace/insert in derived classes)
    //--------------------------------------------------------------------------
    template <typename K>
    [[nodiscard]] constexpr size_type lower_bound_index( K const & key ) const noexcept {
        auto const & keys{ keys_of( storage_ ) };
        return static_cast<size_type>( lower_bound_iter( keys, enreg( this->comp() ), enreg( key ) ) - keys.begin() );
    }
    template <typename K>
    [[nodiscard]] constexpr bool key_eq_at( size_type const pos, K const & key ) const noexcept {
        auto const & keys{ keys_of( storage_ ) };
        return pos < keys.size() && !this->comp()( key, keys[ pos ] );
    }

    // Hint-aware insertion position for multi containers.
    // Returns the hint position if valid (non-strict ordering — equivalents
    // are allowed at the boundary), otherwise narrows the binary search to
    // the relevant half of the container ("as close as possible to hint").
    [[nodiscard, gnu::pure]] constexpr size_type hinted_insert_pos( size_type const hintIdx, Reg auto const key ) const noexcept {
        auto const & keys{ keys_of( storage_ ) };
        auto const   sz  { keys.size() };
        if ( ( hintIdx == 0  || this->leq( keys[ hintIdx - 1 ], key ) ) &&
             ( hintIdx >= sz || this->leq( key, keys[ hintIdx ] ) ) )
            return hintIdx;
        // Narrowed search: upper_bound left (closest to hint from below) or
        // lower_bound right (closest to hint from above).
        auto const wrappedComp{ make_trivially_copyable_predicate( this->comp() ) };
        decltype( auto ) value{ prefetch( this->comp(), key ) };
        if ( hintIdx > 0 && this->le( key, keys[ hintIdx - 1 ] ) )
            return static_cast<size_type>( std::upper_bound( keys.begin(), keys.begin() + static_cast<difference_type>( hintIdx ), value, wrappedComp ) - keys.begin() );
        else
            return static_cast<size_type>( std::lower_bound( keys.begin() + static_cast<difference_type>( hintIdx ), keys.end(), value, wrappedComp ) - keys.begin() );
    }

    //--------------------------------------------------------------------------
    // Deducing-this utilities
    //--------------------------------------------------------------------------

    // Apple Clang does not accept self.unique as a constant expression in
    // constexpr-if / template arguments (deducing-this).
    template <typename Self>
    static constexpr bool is_unique{ std::remove_cvref_t<Self>::unique };

    // Auto-detects unique from derived class
    constexpr void init_sort( this auto && self ) {
        sort_storage<is_unique<decltype( self )>>( self.storage_, self.komp() );
    }

    template <bool WasSorted>
    constexpr void init_sort_merge( this auto && self, size_type const oldSize ) {
        sort_merge_storage<is_unique<decltype( self )>, WasSorted>( self.storage_, self.komp(), oldSize );
    }

    //--------------------------------------------------------------------------
    // Protected swap — type-safe public wrappers in derived classes
    //--------------------------------------------------------------------------
    constexpr void swap_impl( flat_impl & other ) noexcept {
        using std::swap;
        swap( storage_, other.storage_ );
        swap( this->comp(), other.comp() );
    }

    //--------------------------------------------------------------------------
    // Bulk insert helper (exception-safe: clears on sort/merge failure)
    //--------------------------------------------------------------------------
    template <bool WasSorted, typename InputIt>
    constexpr void bulk_insert( this auto && self, InputIt const first, InputIt const last ) {
        auto const oldSize{ self.size() };
        self.storage_.append_range( std::ranges::subrange( first, last ) );
        try {
            self.template init_sort_merge<WasSorted>( oldSize );
        } catch ( ... ) {
            self.clear();
            throw;
        }
    }

    Storage storage_;
}; // class flat_impl

} // namespace detail

using detail::flat_impl;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
