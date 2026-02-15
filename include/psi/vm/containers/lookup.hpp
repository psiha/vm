////////////////////////////////////////////////////////////////////////////////
/// Shared lookup infrastructure for psi::vm sorted associative containers.
///
/// Provides:
///   - LookupType concept   — constrains heterogeneous lookup key types
///   - key_const_arg_t alias — optimal key-passing type for lookup functions
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

#include <concepts>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

/// LookupType — constrains which key types a sorted container's lookup
/// functions accept.
///
/// A type K is a valid lookup key if either:
///   (a) the comparator is transparent (has is_transparent tag), allowing
///       heterogeneous lookup with any comparable type, or
///   (b) K is implicitly convertible to key_type — the conversion happens
///       once at the public API boundary via pass_in_reg, then the optimal
///       representation is forwarded to the internal _impl function.
///       (This subsumes the K == key_type case via identity conversion.)
///
/// This replaces the C++23 pattern of providing two overloads per lookup:
///   iterator find( key_type const & );                          // always
///   template<class K> iterator find( K const & ) requires transparent;  // conditional
/// with a single constrained template — usable in both explicit and abbreviated form:
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


/// key_const_arg_t — optimal key-passing type for sorted container lookup
/// functions.
///
/// Selects the most efficient representation at the public API boundary:
///   - trivial/small keys or transparent comparator → pass_in_reg<Key>
///     (by value for trivials, optimal_const_ref for non-trivials like
///     string → string_view)
///   - non-transparent + non-trivial → Key const &
///     (no wrapping; the comparator requires Key const & and cannot accept
///     optimal_const_ref types like string_view)
template <typename Key, bool transparent_comp>
using key_const_arg_t = std::conditional_t<
    can_be_passed_in_reg<Key> || transparent_comp,
    pass_in_reg<Key>,
    Key const &
>;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
