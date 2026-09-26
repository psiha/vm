////////////////////////////////////////////////////////////////////////////////
/// Comparator traits of the psi::vm sorted containers and sort utilities.
///
/// Contents:
///   - is_simple_comparator<T>   -- trait: can == replace double-negation test?
///   - is_direct_comparator<T>   -- trait: does a comparison read only the keys?
///
/// A header of their own, so that both the Komparator wrapper and the sort
/// front end (psi/vm/sort.hpp, which komparator.hpp includes) can ask them.
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

#include <concepts>
#include <functional>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// The erasure adapters, defined in psi/vm/sort.hpp, which the traits forward
// through.
template <typename Comparator> struct erasure_opt_in;
template <typename Comparator> struct erasure_opt_out;

//==============================================================================
// Comparator traits
//==============================================================================

/// Is this a "simple" comparator where operator== can be used instead of
/// the two-comparison equivalence test?  User specializations are intended.
template <typename T> constexpr bool is_simple_comparator{ false };
template <typename T> constexpr bool is_simple_comparator<std::less   <T>>{ std::is_fundamental_v<T> };
template <typename T> constexpr bool is_simple_comparator<std::greater<T>>{ std::is_fundamental_v<T> };
// Transparent comparators (std::less<>/std::greater<>) just delegate to the
// underlying < and > operators -- they don't redefine ordering semantics, so
// == is safe for any ==-comparable type pair.
template <> inline constexpr bool is_simple_comparator<std::less   <void>>{ true };
template <> inline constexpr bool is_simple_comparator<std::greater<void>>{ true };
// C++20 constrained transparent comparators (same reasoning)
template <> inline constexpr bool is_simple_comparator<std::ranges::less   >{ true };
template <> inline constexpr bool is_simple_comparator<std::ranges::greater>{ true };
// The erasure adapters (sort.hpp) forward the wrapped comparator's simplicity.
template <typename C> inline constexpr bool is_simple_comparator<erasure_opt_in <C>>{ is_simple_comparator<C> };
template <typename C> inline constexpr bool is_simple_comparator<erasure_opt_out<C>>{ is_simple_comparator<C> };


/// Does one comparison read nothing but the keys it is handed?
///
/// is_simple_comparator answers a SEMANTIC question - may == stand in for the
/// double-negation equivalence test - and a search-strategy choice needs a COST
/// one.  They are different properties, and a comparator that treats the key as
/// a handle and fetches what orders it from somewhere else satisfies the first
/// while failing the second: it is exactly as "simple", and every call of it is
/// a scattered load rather than a read of the cache line the caller is already
/// walking.  That is the whole economics of a linear scan, so the scan has to
/// ask this question and not the other one.
///
/// Cost is not deducible from a type, so it is stated rather than derived, and
/// directness is a CLAIM rather than an assumption: a comparator that says
/// nothing gets std::lower_bound.  The opposite default would need a projecting
/// comparator to opt out, leaving the party that knows least - one that says
/// nothing at all - inheriting the strategy that suits it least.  That default is a judgement, not a measurement - an
/// indirect comparison costs the scan and the bisection differently, and which
/// one wins depends on the operation (measured: binary ahead on lookup, the
/// scan ahead on insert, ~14% each way) - so a consumer that has measured its
/// own shape should say so here rather than inherit this.
///
/// std::less/greater name the key type's own ordering, so they read the keys and
/// nothing else however that ordering is spelled.  The transparent forms leave
/// the ordering to whatever the key's comparison operator does, which for a
/// scalar - arithmetic, enumeration, pointer - is a comparison of the key
/// itself, and for a class type is unknown unless the type says so.
///
/// A key type says so with a member
///     static constexpr bool orders_directly{ true };
/// e.g. a strong typedef around an integer with a defaulted <=>.  It is stated
/// on the key rather than by specialising a trait because a key type is
/// visible wherever it is compared, so every translation unit sees the same
/// answer - a trait specialisation that only some of them include would give
/// the same container two definitions.
namespace detail
{
    // the key half of the question, for the comparators that delegate to the
    // key's own ordering
    template <typename Key> constexpr bool key_orders_directly{ std::is_scalar_v<Key> || std::is_same_v<Key, void> };
    template <typename Key> requires requires { { Key::orders_directly } -> std::convertible_to<bool>; }
    constexpr bool key_orders_directly<Key>{ Key::orders_directly };
} // namespace detail

template <typename Comparator, typename Key = void> constexpr bool is_direct_comparator{ false };
template <typename T, typename Key> constexpr bool is_direct_comparator<std::less   <T>, Key>{ true };
template <typename T, typename Key> constexpr bool is_direct_comparator<std::greater<T>, Key>{ true };
template <typename Key> inline constexpr bool is_direct_comparator<std::less   <void>, Key>{ detail::key_orders_directly<Key> };
template <typename Key> inline constexpr bool is_direct_comparator<std::greater<void>, Key>{ detail::key_orders_directly<Key> };
template <typename Key> inline constexpr bool is_direct_comparator<std::ranges::less   , Key>{ detail::key_orders_directly<Key> };
template <typename Key> inline constexpr bool is_direct_comparator<std::ranges::greater, Key>{ detail::key_orders_directly<Key> };
template <typename C, typename Key> inline constexpr bool is_direct_comparator<erasure_opt_in <C>, Key>{ is_direct_comparator<C, Key> };
template <typename C, typename Key> inline constexpr bool is_direct_comparator<erasure_opt_out<C>, Key>{ is_direct_comparator<C, Key> };

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
