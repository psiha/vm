////////////////////////////////////////////////////////////////////////////////
/// Shared lookup infrastructure for psi::vm sorted associative containers.
///
/// Provides:
///   - LookupType concept   -- constrains heterogeneous lookup key types
///   - key_const_arg_t alias -- optimal key-passing type for lookup functions
///
/// Used by flat_set, flat_map, and b+tree families to merge the traditional
/// two-overload lookup pattern (non-template + constrained template) into a
/// single constrained template per function.
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

#include "abi.hpp" // can_be_passed_in_reg, pass_in_reg

#include "komparator.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

/// LookupType -- constrains which key types a sorted container's lookup
/// functions accept.
///
/// A type K is a valid lookup key if either:
///   (a) the comparator is transparent (has is_transparent tag), allowing
///       heterogeneous lookup with any comparable type, or
///   (b) K is implicitly convertible to key_type -- the conversion happens
///       once at the public API boundary via pass_in_reg, then the optimal
///       representation is forwarded to the internal _impl function.
///       (This subsumes the K == key_type case via identity conversion.)
///
/// This replaces the C++23 pattern of providing two overloads per lookup:
///   iterator find( key_type const & );                          // always
///   template<class K> iterator find( K const & ) requires transparent;  // conditional
/// with a single constrained template -- usable in both explicit and abbreviated form:
///   template <LookupType<transparent, key_type> K = key_type>
///   iterator find( K const & );
/// or:
///   iterator find( LookupType<transparent, key_type> auto const & );
///
/// See abi.hpp for the full correctness/optimality analysis of this approach.
template <typename K, bool transparent_comparator, typename StoredKeyType>
concept LookupType =
    transparent_comparator ||
    std::convertible_to<K const &, StoredKeyType const &>;


/// key_const_arg_t -- optimal key-passing type for sorted container lookup
/// functions.
///
/// Selects the most efficient representation at the public API boundary:
///   - trivial/small keys or transparent comparator -> pass_in_reg<Key>
///     (by value for trivials, optimal_const_ref for non-trivials like
///     string -> string_view)
///   - non-transparent + non-trivial -> Key const &
///     (no wrapping; the comparator requires Key const & and cannot accept
///     optimal_const_ref types like string_view)
template <typename Key, bool transparent_comp>
using key_const_arg_t = std::conditional_t<
    can_be_passed_in_reg<Key> || transparent_comp,
    pass_in_reg<Key>,
    Key const &
>;


//==============================================================================
// Sorted-range search primitives: linear vs binary, byte-size dispatched.
//
// For trivially-comparable keys a linear early-exit scan beats std::lower_bound
// on x64 for small ranges: measured (b+tree node search + isolated sorted-array
// probes, clang, -O3) the crossover sits between 1 and 4 KiB of scanned data --
// the same BYTE size for 32-bit and 64-bit keys, so the limit is expressed in
// bytes, not element count. On AArch64 (Apple Silicon measured) clang emits a
// branchless (csel) binary search whose latency is essentially flat in range
// size and which wins at EVERY size -- so the linear path is disabled there.
//==============================================================================

// Range byte-size up to which the dispatched functions below use a linear scan.
inline constexpr std::size_t linear_search_byte_limit
{
#if defined( __aarch64__ ) || defined( _M_ARM64 )
    0
#else
    2048
#endif
};

// Can Key + Comparator use the linear path at all (equivalence of comparator
// equality and ==, trivial copies, small elements)?
template <typename Comparator, typename Key>
constexpr bool linear_search_eligible
{
    is_simple_comparator<Comparator>          &&
    std::is_trivially_copyable_v<Key>         &&
    ( sizeof( Key ) < ( 4 * sizeof( void * ) ) )
}; // linear_search_eligible

// Unconditionally-linear versions (early-exit scans over a sorted range).
template <typename It, typename Comp = std::less<>>
[[ nodiscard, gnu::pure ]] constexpr
It linear_lower_bound( It first, It const last, auto const & key, Comp const & comp = {} ) noexcept
{
    while ( first != last && comp( *first, key ) ) { ++first; }
    return first;
}
template <typename It, typename Comp = std::less<>>
[[ nodiscard, gnu::pure ]] constexpr
It linear_upper_bound( It first, It const last, auto const & key, Comp const & comp = {} ) noexcept
{
    while ( first != last && !comp( key, *first ) ) { ++first; }
    return first;
}
template <typename It, typename Comp = std::less<>>
[[ nodiscard, gnu::pure ]] constexpr
std::optional<It> linear_find( It const first, It const last, auto const & key, Comp const & comp = {} ) noexcept
{
    auto const pos{ linear_lower_bound( first, last, key, comp ) };
    if ( pos == last || comp( key, *pos ) ) { return std::nullopt; }
    return pos;
}

// Runtime-dispatched versions: linear for trivial data & comparators when the
// range is small enough (see linear_search_byte_limit), std:: otherwise.
template <typename It, typename Comp = std::less<>>
[[ nodiscard, gnu::pure ]] constexpr
It lower_bound( It const first, It const last, auto const & key, Comp const & comp = {} ) noexcept
{
    using Key = std::remove_cvref_t<decltype( *first )>;
    if constexpr ( linear_search_eligible<Comp, Key> && ( linear_search_byte_limit != 0 ) )
    {
        if ( static_cast<std::size_t>( last - first ) * sizeof( Key ) <= linear_search_byte_limit ) [[ likely ]]
            return linear_lower_bound( first, last, key, comp );
    }
    return std::lower_bound( first, last, key, comp );
}
template <typename It, typename Comp = std::less<>>
[[ nodiscard, gnu::pure ]] constexpr
It upper_bound( It const first, It const last, auto const & key, Comp const & comp = {} ) noexcept
{
    using Key = std::remove_cvref_t<decltype( *first )>;
    if constexpr ( linear_search_eligible<Comp, Key> && ( linear_search_byte_limit != 0 ) )
    {
        if ( static_cast<std::size_t>( last - first ) * sizeof( Key ) <= linear_search_byte_limit ) [[ likely ]]
            return linear_upper_bound( first, last, key, comp );
    }
    return std::upper_bound( first, last, key, comp );
}
template <typename It, typename Comp = std::less<>>
[[ nodiscard, gnu::pure ]] constexpr
std::optional<It> find( It const first, It const last, auto const & key, Comp const & comp = {} ) noexcept
{
    auto const pos{ psi::vm::lower_bound( first, last, key, comp ) };
    if ( pos == last || comp( key, *pos ) ) { return std::nullopt; }
    return pos;
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
