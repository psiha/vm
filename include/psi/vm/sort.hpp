////////////////////////////////////////////////////////////////////////////////
/// psi::vm::sort -- reusable pdqsort front end with OPT-IN comparator/iterator
/// type erasure.
///
/// The erasure trades a perfectly-predicted indirect comparator call for
/// collapsing the per-(iterator, comparator) pdqsort instantiation family to
/// one shared worker per key type -- a binary-size (icache) optimisation that
/// only pays off for "fat" (non-register-passable) comparators. Because it is
/// a trade-off, it is strictly OPT-IN:
///   * explicitly, via the Erasure template parameter of sort(); or
///   * per comparator type, by giving the comparator a
///     `static constexpr bool allow_comparator_erasure{ true };` member --
///     honoured by the psi::vm sorted containers (flat_set/flat_map/b+tree via
///     Komparator) so a user picks the size-optimised-erased vs
///     fully-typed-monomorphic shape per container instantiation.
/// Even when allowed, erasure is ATTEMPTED only when the internal heuristics
/// say it can win (see erasure_applies below).
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

#include "containers/abi.hpp" // can_be_passed_in_reg, make_trivially_copyable_predicate
#include "containers/comparator_traits.hpp" // is_direct_comparator
#include "erased_ref_predicate.hpp"

#if __has_include( <boost/sort/pdqsort/pdqsort.hpp> )
#include <boost/sort/pdqsort/pdqsort.hpp>
#define PSI_VM_PDQSORT(            first, last, comp ) boost::sort::pdqsort           ( first, last, comp )
#define PSI_VM_PDQSORT_BRANCHLESS( first, last, comp ) boost::sort::pdqsort_branchless( first, last, comp )
#else
#include <boost/move/algo/detail/pdqsort.hpp>
#define PSI_VM_PDQSORT(            first, last, comp ) boost::movelib::pdqsort( first, last, comp )
#define PSI_VM_PDQSORT_BRANCHLESS( first, last, comp ) boost::movelib::pdqsort( first, last, comp )
#endif

#include <iterator>
#include <memory>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

/// Compile-time erasure policy for the sort/merge utilities and, through the
/// comparator opt-in trait, the sorted containers.
enum struct comparator_erasure : std::uint8_t
{
    never,  ///< fully typed, monomorphic codegen at every call site
    allowed ///< attempt type erasure when the internal heuristics deem it a win
}; // enum struct comparator_erasure

/// Comparator opt-in trait: a comparator type carrying
/// `static constexpr bool allow_comparator_erasure{ true };` selects
/// comparator_erasure::allowed wherever the containers sort or merge with it.
template <typename Comparator>
constexpr comparator_erasure comparator_erasure_of{
    requires { requires( Comparator::allow_comparator_erasure ); }
        ? comparator_erasure::allowed
        : comparator_erasure::never
};

/// Which of pdqsort's two partitionings sort() uses. The branchless one (block
/// partitioning) removes the branch on each comparison's result at the price
/// of more work per element, so it wins when a comparison is a single cheap
/// compare of keys that are cheap to move, and can lose when the comparison
/// branches itself or the keys are large.
///  * automatic  -- branchless for one of the standard comparators over a
///                  scalar key (see key_sort_detail::branchless_by_default),
///                  branching otherwise;
///  * branchless -- always branchless;
///  * branching  -- always branching, also for the comparisons Boost.Sort's
///                  pdqsort would otherwise partition branchlessly by itself.
/// Without Boost.Sort both are Boost.Move's pdqsort, which only branches.
enum struct sort_partitioning : std::uint8_t
{
    automatic,
    branchless,
    branching
}; // enum struct sort_partitioning

/// Comparator opt-in/opt-out trait: a comparator type carrying
/// `static constexpr bool is_branchless{ true };` selects
/// sort_partitioning::branchless wherever the containers sort with it,
/// `{ false }` selects sort_partitioning::branching, and one that states
/// nothing gets sort_partitioning::automatic.
template <typename Comparator>
constexpr sort_partitioning comparator_partitioning_of{
    requires { requires(  Comparator::is_branchless ); } ? sort_partitioning::branchless :
    requires { requires( !Comparator::is_branchless ); } ? sort_partitioning::branching  :
                                                           sort_partitioning::automatic
};

/// Container-level opt-in/opt-out adapters: wrap the comparator at the
/// container instantiation site —
///     flat_set<K, erasure_opt_in<MyComp>>
///     bp_tree <T, erasure_opt_in<MyComp>>
/// — to select the type-erased sort/merge codegen for THAT container type
/// only, leaving the comparator type itself untouched (whose own
/// allow_comparator_erasure member remains the type-wide alternative).
/// Aggregates deriving from the comparator: EBO-friendly, inherit the full
/// comparator interface (eq/leq/val/sort customizations, is_transparent),
/// preserve trivial-copyability/aggregate initialization, and — being
/// distinct types — give differently-policied containers distinct types,
/// exactly as a container NTTP would.
template <typename Comparator>
struct erasure_opt_in : Comparator
{
    static constexpr bool allow_comparator_erasure{ true };
};

template <typename Comparator>
struct erasure_opt_out : Comparator
{
    static constexpr bool allow_comparator_erasure{ false };
};

namespace detail
{
    /// The heuristic gate: erasure can win only for a fat (non-reg-passable)
    /// comparator -- the ones make_trivially_copyable_predicate mints a
    /// distinct by-ref closure type for, re-stamping the algorithm family per
    /// comparator type -- over register-scalar keys (the erased predicate
    /// passes keys by value).
    template <typename Comparator, typename Key>
    bool constexpr erasure_applies{ !can_be_passed_in_reg<Comparator> && std::is_scalar_v<Key> && can_be_passed_in_reg<Key> };

    // Out-of-line pdqsort entries for the type-erased comparator path. The
    // noinline barrier is load-bearing: erased_ref_predicate's thunk pointer
    // is a compile-time constant at every call site, so an inlined sort body
    // gets the indirect call devirtualized and the whole pdqsort family
    // re-duplicated per caller (defeating the erasure; observed with LTO on
    // linux). Kept out-of-line the family is stamped once and the thunk stays
    // a shared indirect call.
    //
    // Two granularities:
    //  * erased_ptr_sort -- contiguous ranges, iterators STRIPPED to naked
    //    pointers: ONE instantiation per key type serves every caller.
    //  * erased_iter_sort -- non-contiguous random-access iterators: the
    //    comparator axis is still collapsed, one instantiation per
    //    (iterator, key) remains.
    template <bool Branchless, typename T>
    [[ gnu::noinline ]] void erased_ptr_sort( T * __restrict const first, T * __restrict const last, erased_ref_predicate<T> const comp ) noexcept
    {
        if constexpr ( Branchless )
            PSI_VM_PDQSORT_BRANCHLESS( first, last, comp );
        else
            PSI_VM_PDQSORT( first, last, comp );
    }

    template <bool Branchless, std::random_access_iterator It>
    [[ gnu::noinline ]] void erased_iter_sort( It const first, It const last, erased_ref_predicate<std::iter_value_t<It>> const comp ) noexcept
    {
        if constexpr ( Branchless )
            PSI_VM_PDQSORT_BRANCHLESS( first, last, comp );
        else
            PSI_VM_PDQSORT( first, last, comp );
    }
} // namespace detail

namespace key_sort_detail
{
    // What sort_partitioning::automatic picks: the branchless partitioning for
    // one of the standard comparators (is_direct_comparator) over a scalar key
    // - arithmetic, enumeration, pointer. That extends Boost.Sort's own rule,
    // std::less and std::greater over an arithmetic key, to std::ranges::less
    // and greater, to enumeration and pointer keys, and to the erasure
    // adapters around any of those. A class key does not get it, not even one
    // that states orders_directly: that promises that a comparison reads
    // nothing but the key, not that the comparison is free of branches
    // (several members compared in turn) or that the key is cheap to move, and
    // the containers' sorts (Komparator::sort) come through here too.
    // Both partitionings sort unstably, so the relative order of keys that
    // compare equal without being the same - a floating-point -0.0 and +0.0 -
    // is unspecified with either, and the two can leave them in different
    // orders.
    template <typename Comparator, typename Key>
    bool constexpr branchless_by_default{ is_direct_comparator<Comparator, Key> && std::is_scalar_v<Key> };

    // Boost.Sort's pdqsort partitions branchlessly by itself for std::less and
    // std::greater over an arithmetic key; a predicate of any other type keeps
    // it on the branching partition.
    template <typename Predicate>
    struct branching_predicate
    {
        Predicate predicate;

        constexpr bool operator()( auto const & left, auto const & right ) const noexcept( noexcept( predicate( left, right ) ) ) { return predicate( left, right ); }
    };
} // namespace key_sort_detail

/// pdqsort front end. Erasure (when allowed AND applicable per the heuristic)
/// routes through a [[gnu::noinline]] type-erased worker -- over naked
/// pointers for contiguous ranges -- so one worker instantiation serves all
/// callers; otherwise fully-typed pdqsort with the trivially-copyable
/// predicate wrapper. Partitioning picks between pdqsort's branchless and
/// branching variants (see sort_partitioning).
template <comparator_erasure Erasure = comparator_erasure::never, sort_partitioning Partitioning = sort_partitioning::automatic, std::random_access_iterator It, typename Comparator>
constexpr void sort( It const first, It const last, Comparator const & __restrict comp ) noexcept
{
    using key_t = std::iter_value_t<It>;
    bool constexpr branchless
    {
        Partitioning == sort_partitioning::branchless ||
        ( Partitioning == sort_partitioning::automatic && key_sort_detail::branchless_by_default<Comparator, key_t> )
    };
    if constexpr ( Erasure == comparator_erasure::allowed && detail::erasure_applies<Comparator, key_t> )
    {
        if !consteval
        {
            if constexpr ( std::contiguous_iterator<It> )
                detail::erased_ptr_sort<branchless>( std::to_address( first ), std::to_address( last ), erased_ref_predicate<key_t>::bind( comp ) );
            else
                detail::erased_iter_sort<branchless>( first, last, erased_ref_predicate<key_t>::bind( comp ) );
            return;
        }
    }
    if constexpr ( branchless )
    {
        PSI_VM_PDQSORT_BRANCHLESS( first, last, make_trivially_copyable_predicate( comp ) );
    }
    else if constexpr ( Partitioning == sort_partitioning::branching )
    {
        using predicate_t = std::remove_cvref_t<decltype( make_trivially_copyable_predicate( comp ) )>;
        PSI_VM_PDQSORT( first, last, key_sort_detail::branching_predicate<predicate_t>{ make_trivially_copyable_predicate( comp ) } );
    }
    else
    {
        // automatic, where Boost.Sort's own rule never picks the branchless
        // partitioning either (see branchless_by_default)
        PSI_VM_PDQSORT( first, last, make_trivially_copyable_predicate( comp ) );
    }
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
