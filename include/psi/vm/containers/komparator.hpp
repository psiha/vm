////////////////////////////////////////////////////////////////////////////////
/// Comparator traits, utilities and the Komparator wrapper for psi::vm sorted
/// containers.
///
/// Contents:
///   - is_simple_comparator<T>   -- trait: can == replace double-negation test?
///   - comp_eq(comp, a, b)       -- optimised equality from strict-weak comparator
///   - Komparator<Comparator>    -- EBO wrapper with lt/gt/eq/le/ge + sort
///
/// Containers (flat_set, flat_map, b+tree) inherit from Komparator to get
/// zero-overhead comparator storage + derived comparison helpers + sort.
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
#include "../sort.hpp"

#include <functional>
#include <iterator>
#include <ranges>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

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


//==============================================================================
// comp_eq -- optimised equality from a strict-weak comparator (free function)
//==============================================================================

/// Three-tier dispatch:
///   1. Custom comp.eq() if available
///   2. Direct == for simple comparators (std::less/greater on fundamentals, transparent comparators)
///   3. Standard two-comparison equivalence (!comp(a,b) && !comp(b,a))
template <typename Comp>
[[ gnu::pure ]] constexpr bool comp_eq( Comp const & comp, auto const & left, auto const & right ) noexcept
{
    if constexpr ( requires{ comp.eq( left, right ); } )
        return comp.eq( left, right );
    if constexpr ( is_simple_comparator<Comp> && requires{ left == right; } )
        return left == right;
    return !comp( left, right ) && !comp( right, left );
}


//==============================================================================
// Komparator -- comparator wrapper (EBO via public inheritance)
//==============================================================================

/// Publicly inherits from Comparator for empty-base optimisation. Being an
/// aggregate (no user-declared constructors, public base, no data members)
/// means no forwarding constructors are needed -- aggregate initialization
/// handles all cases: Komparator<C>{ c } or Komparator<C>{}.
///
/// Provides derived comparison operations (lt, gt, eq, le, ge) and a
/// sort() method that dispatches to the best available pdqsort variant.
template <typename Comparator>
struct Komparator : Comparator
{
    /// True if Comparator supports heterogeneous lookup (has is_transparent tag)
    static constexpr bool transparent_comparator{ requires{ typename Comparator::is_transparent; } };

    [[ nodiscard ]] constexpr Comparator const & comp() const noexcept { return *this; }
    [[ nodiscard ]] constexpr Comparator       & comp()       noexcept { return *this; }

    [[ gnu::pure ]] constexpr bool lt( auto const & left, auto const & right ) const noexcept { return comp()( left, right ); }
    [[ gnu::pure ]] constexpr bool gt( auto const & left, auto const & right ) const noexcept { return comp()( right, left ); }
    [[ gnu::pure ]] constexpr bool eq( auto const & left, auto const & right ) const noexcept { return comp_eq( comp(), left, right ); }
    [[ gnu::pure ]] constexpr bool le( auto const & left, auto const & right ) const noexcept
    {
        if constexpr ( requires{ comp().leq( left, right ); } )
            return comp().leq( left, right );
        return !comp()( right, left );
    }
    [[ gnu::pure ]] constexpr bool ge( auto const & left, auto const & right ) const noexcept
    {
        if constexpr ( requires{ comp().geq( left, right ); } )
            return comp().geq( left, right );
        return !comp()( left, right );
    }

    /// Comparator-trait-derived erasure policy (see psi/vm/sort.hpp): the
    /// containers sort and merge with this, so a user picks the
    /// size-optimised-erased vs fully-typed-monomorphic shape per container
    /// instantiation through the comparator's allow_comparator_erasure member.
    static constexpr comparator_erasure erasure{ comparator_erasure_of<Comparator> };

    /// Sort a range using the best available algorithm:
    ///   1. Comparator's own sort() if provided (e.g. radix sort)
    ///   2. pdqsort_branchless if Comparator::is_branchless
    ///   3. pdqsort (default fallback)
    /// (2) and (3) route through psi::vm::sort with the trait-derived erasure
    /// policy above.
    template <std::random_access_iterator It>
    constexpr void sort( It const first, It const last ) const noexcept
    {
        if constexpr ( requires{ comp().sort( first, last ); } )
            comp().sort( first, last );
        else if constexpr ( requires{ Comparator::is_branchless; requires( Comparator::is_branchless ); } )
            vm::sort<erasure, true >( first, last, comp() );
        else
            vm::sort<erasure, false>( first, last, comp() );
    }
}; // struct Komparator


/// Pre-fetches the comparison value from a key.
/// If the comparator provides a val() method (for indirect comparisons via
/// pointers/IDs), calls it once to avoid repeated fetches during binary search.
/// Otherwise returns the key as-is.
template <typename Comp, typename K>
constexpr decltype( auto ) prefetch( Comp const & comp, K const & key ) noexcept {
    if constexpr ( requires { comp.val( key ); } )
        return comp.val( key );
    else
        return unwrap( key ); // parenthesized: decltype(auto) returns K const &
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
