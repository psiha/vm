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

#if __has_include( <boost/sort/pdqsort/pdqsort.hpp> )
#include <boost/sort/pdqsort/pdqsort.hpp>
#define PSI_VM_PDQSORT(            first, last, comp ) boost::sort::pdqsort           ( first, last, comp )
#define PSI_VM_PDQSORT_BRANCHLESS( first, last, comp ) boost::sort::pdqsort_branchless( first, last, comp )
#else
#include <boost/move/algo/detail/pdqsort.hpp>
#define PSI_VM_PDQSORT(            first, last, comp ) boost::movelib::pdqsort( first, last, comp )
#define PSI_VM_PDQSORT_BRANCHLESS( first, last, comp ) boost::movelib::pdqsort( first, last, comp )
#endif

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


namespace detail
{
    // Out-of-line pdqsort entry for the type-erased comparator path. The
    // noinline barrier is load-bearing: erased_ref_predicate's thunk pointer
    // is a compile-time constant at every Komparator::sort call site, so an
    // inlined sort body gets the indirect call devirtualized and the whole
    // pdqsort family re-duplicated per caller (defeating the erasure; observed
    // with LTO on linux). Kept out-of-line the family is stamped once per
    // (iterator, key) and the thunk stays a shared indirect call.
    template <bool Branchless, std::random_access_iterator It>
    [[ gnu::noinline ]] void erased_sort( It const first, It const last, erased_ref_predicate<std::iter_value_t<It>> const comp ) noexcept
    {
        if constexpr ( Branchless )
            PSI_VM_PDQSORT_BRANCHLESS( first, last, comp );
        else
            PSI_VM_PDQSORT( first, last, comp );
    }
} // namespace detail

//==============================================================================
// erased_pdqsort -- public type-erased pdqsort over register-scalar keys
//==============================================================================

/// Sort [first, last) of register-scalar keys with an arbitrary strict-weak
/// predicate WITHOUT stamping a pdqsort family per predicate type: the
/// predicate is erased behind erased_ref_predicate (a (thunk, ref) pair using
/// the SysV convention where available -- see abi.hpp), so the whole pdqsort
/// instantiation is shared once per (iterator, key) pair across every caller
/// and every predicate type. The out-of-line detail::erased_sort body is the
/// load-bearing LTO devirtualization barrier: the thunk pointer is a
/// compile-time constant at each call site and an inlined sort body would get
/// the indirect call devirtualized and the family re-duplicated per caller.
///
/// The predicate is captured BY REFERENCE and must outlive the call (pass it
/// directly -- never a temporary bound elsewhere). Intended for callers whose
/// predicate is too large to pass in a register or is a per-call-site closure
/// type (index argsorts, column comparators); for register-passable trivial
/// predicates over scalar keys the direct (non-erased) pdqsort is typically
/// better -- Komparator::sort below performs exactly that dispatch.
template <bool Branchless = false, std::random_access_iterator It, typename Pred>
requires std::is_scalar_v<std::iter_value_t<It>>
void erased_pdqsort( It const first, It const last, Pred const & __restrict pred ) noexcept
{
    detail::erased_sort<Branchless>(
        first, last, erased_ref_predicate<std::iter_value_t<It>>::bind( pred )
    );
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

    /// Sort a range using the best available algorithm:
    ///   1. Comparator's own sort() if provided (e.g. radix sort)
    ///   2. pdqsort_branchless if Comparator::is_branchless
    ///   3. pdqsort (default fallback)
    template <std::random_access_iterator It>
    constexpr void sort( It const first, It const last ) const noexcept
    {
        // For non-reg-passable comparators over register-scalar keys erase the
        // comparator type: the by-ref wrapper closure otherwise minted by
        // make_trivially_copyable_predicate is a distinct type per deduced
        // Pred, i.e. per Comparator here, re-stamping the whole pdqsort family
        // once per comparator type (see erased_ref_predicate in abi.hpp).
        constexpr bool erase_comparator{ !can_be_passed_in_reg<Comparator> && std::is_scalar_v<std::iter_value_t<It>> };
        if constexpr ( requires{ comp().sort( first, last ); } )
            comp().sort( first, last );
        else if constexpr ( requires{ Comparator::is_branchless; requires( Comparator::is_branchless ); } ) {
            if constexpr ( erase_comparator )
                erased_pdqsort<true >( first, last, comp() );
            else
                PSI_VM_PDQSORT_BRANCHLESS( first, last, make_trivially_copyable_predicate( comp() ) );
        } else {
            if constexpr ( erase_comparator )
                erased_pdqsort<false>( first, last, comp() );
            else
                PSI_VM_PDQSORT( first, last, make_trivially_copyable_predicate( comp() ) );
        }
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
