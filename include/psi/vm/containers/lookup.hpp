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
// Sorted-range search primitives: linear vs binary, dispatched on the number of
// VALUES in the range.
//
// Why values and not bytes.  The limit used to be denominated in bytes, on the
// grounds that the crossover had been found at the same byte size for 32- and
// 64-bit keys.  Re-measured with both widths in a real tree (test/b+tree.cpp,
// bp_tree.benchmark_key_width), it is not: the crossover sits between ~250 and
// ~510 VALUES for 4-byte keys AND for 8-byte keys, which is 1004-2028 bytes in
// one case and 2024-4072 in the other.  Denominated in bytes, one constant
// therefore means two different policies for two widths of the same type.
//
// Why AArch64 is zero.  clang emits a genuinely branchless binary search there -
// `fcmp; csel; csel; cbnz`, no data-dependent branch - and binary wins at every
// length and every key type measured, floating point included (48-289% ahead).
//
// ⚠ It is NOT simply "branchless wins, branchy loses".  clang-cl emits a
// branchless integer binary search on Windows too (`cmovae` under the MSVC STL,
// where libstdc++ on the same ISA emits `cmp;jae`), and a linear scan still won
// in-tree there up to ~250-510 values.  So what keeps the scan competitive
// inside a b+tree is not branch misprediction: it is that a node has just been
// pointer-chased to and is cold, where a scan streams it under the prefetcher
// while binary search issues dependent misses.  The same code measured on a
// RESIDENT array reverses the verdict (binary wins from 8 values), which is why
// these numbers may only be re-tuned from an in-tree measurement.
//
// The limit therefore depends on the ISA *and* on the key type, because those
// are the two axes the measurements actually separate:
//
//   ISA        integral keys                 floating-point keys
//   AArch64    binary always                 binary always
//   x86-64     linear to 256 values          linear to 128 values
//
// Integers: in-tree crossover ~250-510 values, at BOTH 4- and 8-byte widths
// (bp_tree.benchmark_key_width); 256 sits at the safe end of that band.
// Floating point: no in-tree data - psi::vm's own trees are not float-keyed -
// so this is the isolated measurement, where float and double cross together at
// 96 (Xeon 8581C) / 192 (Zen 5) / 256 (Arrow Lake).  128 is below all three, and
// at 128 a scan is still ahead on every x86 box measured (26.6% on Zen 5, a wash
// on the Xeon).  Being the conservative end of an isolated measurement, it is
// the number to revisit first if a float-keyed tree ever appears.
//
// test/lookup_threshold.cpp prints both regimes per type and flags every length
// where this policy disagrees with what it measures.
//==============================================================================

// Number of values up to which the dispatched functions below use a linear scan
// (0 disables the linear path entirely).  Overridable so the dispatch can be
// A/B'd without editing this header.
template <typename Key>
inline constexpr std::size_t linear_search_max_values
{
#if defined( PSI_VM_LINEAR_SEARCH_MAX_VALUES )
    PSI_VM_LINEAR_SEARCH_MAX_VALUES
#elif defined( __aarch64__ ) || defined( _M_ARM64 )
    0
#else
    std::is_floating_point_v<Key> ? 128 : 256
#endif
};

// Can Key + Comparator use the linear path at all?  Four conditions, and the
// first two are separate questions about the comparator that a single trait
// used to answer together (komparator.hpp): the scan needs comparator equality
// to BE ==, and it needs a comparison to read only the keys it is walking.  An
// indirect comparator satisfies the first and not the second, and it does not
// keep the same crossover: measured against the b+tree benchmark's indirect
// comparator at 512-byte nodes, the two strategies trade places by ~14% in
// opposite directions - binary ahead on lookup, the scan ahead on insert - so
// the choice is not the one the length threshold alone was calibrated for.
template <typename Comparator, typename Key>
constexpr bool linear_search_eligible
{
    is_simple_comparator<Comparator>          &&
    is_direct_comparator<Comparator>          &&
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
// range is short enough (see linear_search_max_values), std:: otherwise.
template <typename It, typename Comp = std::less<>>
[[ nodiscard, gnu::pure ]] constexpr
It lower_bound( It const first, It const last, auto const & key, Comp const & comp = {} ) noexcept
{
    using Key = std::remove_cvref_t<decltype( *first )>;
    if constexpr ( linear_search_eligible<Comp, Key> && ( linear_search_max_values<Key> != 0 ) )
    {
        if ( static_cast<std::size_t>( last - first ) <= linear_search_max_values<Key> ) [[ likely ]]
            return linear_lower_bound( first, last, key, comp );
    }
    return std::lower_bound( first, last, key, comp );
}
template <typename It, typename Comp = std::less<>>
[[ nodiscard, gnu::pure ]] constexpr
It upper_bound( It const first, It const last, auto const & key, Comp const & comp = {} ) noexcept
{
    using Key = std::remove_cvref_t<decltype( *first )>;
    if constexpr ( linear_search_eligible<Comp, Key> && ( linear_search_max_values<Key> != 0 ) )
    {
        if ( static_cast<std::size_t>( last - first ) <= linear_search_max_values<Key> ) [[ likely ]]
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
