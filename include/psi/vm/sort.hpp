////////////////////////////////////////////////////////////////////////////////
/// psi::vm::sort -- reusable pdqsort front end with OPT-IN comparator/iterator
/// type erasure, and sort_unique, the sort followed by deduplication.
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

#include "containers/abi.hpp" // can_be_passed_in_reg, make_trivially_copyable_predicate, optimal_const_ref
#include "containers/comparator_traits.hpp" // is_direct_comparator
#include "erased_ref_predicate.hpp"

#include <psi/build/no_unique_address.hpp>

#if __has_include( <boost/sort/pdqsort/pdqsort.hpp> )
#include <boost/sort/pdqsort/pdqsort.hpp>
#define PSI_VM_PDQSORT(            first, last, comp ) boost::sort::pdqsort           ( first, last, comp )
#define PSI_VM_PDQSORT_BRANCHLESS( first, last, comp ) boost::sort::pdqsort_branchless( first, last, comp )
#else
#include <boost/move/algo/detail/pdqsort.hpp>
#define PSI_VM_PDQSORT(            first, last, comp ) boost::movelib::pdqsort( first, last, comp )
#define PSI_VM_PDQSORT_BRANCHLESS( first, last, comp ) boost::movelib::pdqsort( first, last, comp )
#endif

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>
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

    // A register-sized part is held by value, so an empty comparator and the
    // identity projection take no space. Anything larger is an
    // optimal_const_ref (a const reference, or a view): the predicate stays
    // trivially copyable, and make_trivially_copyable_predicate passes it on
    // unchanged. Not pass_in_reg -- that wrapper is for a value parameter at
    // a function boundary, and pdqsort already receives the predicate through
    // make_trivially_copyable_predicate.
    template <typename T>
    using projected_part = std::conditional_t<can_be_passed_in_reg<T>, T, typename optimal_const_ref<T>::type>;

    template <typename Comparator, typename Projection>
    struct projected_predicate
    {
        PSI_NO_UNIQUE_ADDRESS projected_part<Comparator> comparator;
        PSI_NO_UNIQUE_ADDRESS projected_part<Projection> projection;

        constexpr bool operator()( auto const & left, auto const & right ) const
            noexcept( noexcept( comparator( std::invoke( projection, left ), std::invoke( projection, right ) ) ) )
        {
            return comparator( std::invoke( projection, left ), std::invoke( projection, right ) );
        }
    };
} // namespace detail

namespace key_sort_detail
{
    // What sort_partitioning::automatic picks: the branchless partitioning for
    // one of the standard comparators (is_direct_comparator) over a scalar key
    // - arithmetic (the 128-bit integers too, see detail::is_scalar_key),
    // enumeration, pointer. That extends Boost.Sort's own rule,
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
    // A class key that is a single integer in a struct (a strong typedef, a
    // word-sized view of packed bytes) therefore gets the BRANCHING partition
    // unless it says otherwise: it states `static constexpr bool
    // is_branchless{ true };` (detail::key_states_branchless), or the call
    // passes sort_partitioning::branchless, or the comparator states it.
    template <typename Comparator, typename Key>
    bool constexpr branchless_by_default{ is_direct_comparator<Comparator, Key> && ( detail::is_scalar_key<Key> || detail::key_states_branchless<Key> ) };

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
/// branching variants (see sort_partitioning). The range overload sorts that
/// same range. A projection -- a member pointer, as std::ranges::sort takes --
/// is applied before the comparator. The identity projection is not wrapped,
/// so a scalar key compared by std::ranges::less still partitions branchlessly.
/// A projection that throws terminates, as a throwing comparator does.
template <comparator_erasure Erasure = comparator_erasure::never, sort_partitioning Partitioning = sort_partitioning::automatic, std::random_access_iterator It, typename Comparator = std::ranges::less, typename Projection = std::identity>
constexpr void sort( It const first, It const last, Comparator const & __restrict comp = {}, [[ maybe_unused ]] Projection const & proj = {} ) noexcept
{
    if constexpr ( !std::same_as<std::remove_cvref_t<Projection>, std::identity> )
    {
        vm::sort<Erasure, Partitioning>( first, last, detail::projected_predicate<Comparator, Projection>{ comp, proj } );
    }
    else
    {
        // pdqsort is not a constant expression. A compile-time call uses
        // std::sort, only for a contiguous range of an arithmetic key. Both
        // arms of `if consteval` are instantiated, and std::sort cannot take
        // a proxy iterator even when its value type is arithmetic, so a
        // runtime-only call through one would fail to compile without the
        // contiguous requirement.
        if constexpr ( std::contiguous_iterator<It> && std::is_arithmetic_v<std::remove_cv_t<std::iter_value_t<It>>> )
        {
            if consteval
            {
                std::sort( first, last, comp );
                return;
            }
        }
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
}

template <comparator_erasure Erasure = comparator_erasure::never, sort_partitioning Partitioning = sort_partitioning::automatic, std::ranges::random_access_range Range, typename Comparator = std::ranges::less, typename Projection = std::identity>
requires std::same_as<std::ranges::iterator_t<Range>, std::ranges::sentinel_t<Range>>
      && std::indirect_strict_weak_order<Comparator, std::projected<std::ranges::iterator_t<Range>, Projection>>
constexpr void sort( Range && range, Comparator const & comp = {}, Projection const & proj = {} ) noexcept
{
    vm::sort<Erasure, Partitioning>( std::ranges::begin( range ), std::ranges::end( range ), comp, proj );
}

/// Sorts the range by comp, as sort() does, and moves the first of each run of
/// equivalent elements to the front, in order; returns the end of those
/// elements. The elements past it are left valid but with unspecified values.
/// Any strict weak ordering serves, e.g. std::ranges::lexicographical_compare
/// for a range of ranges, or a comparator that orders by one member only. Two
/// elements are equivalent when neither orders before the other. Once the
/// range is sorted no element orders before the last one kept, so an element
/// is equivalent to it exactly when the kept one does not order before it:
/// one comparison per element, and no equality operator needed (std::unique
/// would want an equivalence relation, which that one-sided test is not). The
/// sort is not stable, so of a run of equivalent elements that are not
/// identical which one is kept is unspecified. A projection, when one is
/// passed, is applied before the comparator, so those runs are runs of equal
/// projected keys and an unspecified element of each run is kept. noexcept
/// as sort() is: a comparator, a projection or an element move that throws
/// terminates.
template <comparator_erasure Erasure = comparator_erasure::never, sort_partitioning Partitioning = sort_partitioning::automatic, std::random_access_iterator It, typename Comparator = std::ranges::less, typename Projection = std::identity>
[[ nodiscard ]] constexpr It sort_unique( It const first, It const last, Comparator const & comp = {}, [[ maybe_unused ]] Projection const & proj = {} ) noexcept
{
    if constexpr ( !std::same_as<std::remove_cvref_t<Projection>, std::identity> )
    {
        return vm::sort_unique<Erasure, Partitioning>( first, last, detail::projected_predicate<Comparator, Projection>{ comp, proj } );
    }
    else
    {
        vm::sort<Erasure, Partitioning>( first, last, comp );
        if ( first == last )
            return last;
        auto kept{ first };
        using value_t = std::iter_value_t<It>;
        if constexpr ( std::is_trivially_copyable_v<value_t> && sizeof( value_t ) <= 2 * sizeof( void * ) )
        {
            // The last kept element held as a value: comparing against *kept
            // would read back the element just written, and through a proxy
            // iterator that writes an element as several pieces (packed records
            // of an odd width) such a load spans more than one store still in
            // flight and cannot be forwarded from them - it waits until they
            // reach the cache.
            value_t kept_value( *kept );
            for ( auto next{ std::next( first ) }; next != last; ++next )
            {
                value_t const value( *next );
                if ( comp( kept_value, value ) )
                {
                    if ( ++kept != next )
                        *kept = std::move( *next );
                    kept_value = value;
                }
            }
        }
        else
        {
            for ( auto next{ std::next( first ) }; next != last; ++next )
            {
                if ( comp( *kept, *next ) && ++kept != next )
                    *kept = std::move( *next );
            }
        }
        return std::next( kept );
    }
}

/// Sorts and deduplicates a container as the iterator overload does and
/// truncates it to the kept elements. noexcept on the same terms, and on the
/// assumption that resize() to a smaller size does not throw: true of the
/// standard and psi::vm containers, whose shrinking resize neither allocates
/// nor constructs, but a property of the container rather than one checked
/// here - resize() is noexcept for none of them, as growing allocates.
template <comparator_erasure Erasure = comparator_erasure::never, sort_partitioning Partitioning = sort_partitioning::automatic, std::ranges::random_access_range Container, typename Comparator = std::ranges::less, typename Projection = std::identity>
requires requires( Container & container ) { container.resize( std::ranges::size( container ) ); }
constexpr void sort_unique( Container & container, Comparator const & comp = {}, Projection const & proj = {} ) noexcept
{
    auto const first{ std::ranges::begin( container ) };
    auto const kept { vm::sort_unique<Erasure, Partitioning>( first, std::ranges::end( container ), comp, proj ) };
    container.resize( static_cast<std::ranges::range_size_t<Container>>( kept - first ) );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
