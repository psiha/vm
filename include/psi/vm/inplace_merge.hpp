////////////////////////////////////////////////////////////////////////////////
/// psi::vm::inplace_merge -- reusable in-place merge (boost::movelib::
/// adaptive_merge seam) with OPT-IN comparator/iterator type erasure.
///
/// Same policy as psi/vm/sort.hpp: erasure collapses the per-comparator
/// adaptive_merge instantiation family (measured downstream: ~25 KB per
/// instantiation over `unsigned int *`) to one [[gnu::noinline]] worker per
/// key type -- iterators stripped to naked pointers -- but adds an indirect
/// comparator call, so it is strictly opt-in via the Erasure parameter (or,
/// through the containers, the comparator's allow_comparator_erasure trait).
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

#include "sort.hpp" // comparator_erasure, erasure_applies, erased_ref_predicate

#if __has_include( <boost/move/algo/adaptive_merge.hpp> )
#include <boost/move/algo/adaptive_merge.hpp>
#define PSI_VM_HAS_ADAPTIVE_MERGE 1
#else
#define PSI_VM_HAS_ADAPTIVE_MERGE 0
#endif

#include <algorithm>
#include <iterator>
#include <memory>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace detail
{
#if PSI_VM_HAS_ADAPTIVE_MERGE
    // Out-of-line adaptive_merge behind a type-erased comparator over naked
    // pointers: one instantiation per key type serves every caller. COLD path
    // (bulk insert / index build, never a hot per-element loop), so the erased
    // indirect comparator call carries no hot-loop cost. [[gnu::noinline]]
    // keeps the out-of-line boundary so LTO cannot constant-fold the thunk
    // address and re-duplicate the merge body per caller (the
    // erased-fn-pointer devirtualization trap).
    template <typename T>
    [[ gnu::noinline ]] void erased_adaptive_merge(
        T * const first, T * const middle, T * const last,
        erased_ref_predicate<T> const comp, T * const buffer, std::size_t const bufLen
    ) noexcept {
        boost::movelib::adaptive_merge( first, middle, last, comp, buffer, bufLen );
    }
#endif // PSI_VM_HAS_ADAPTIVE_MERGE
} // namespace detail

/// In-place merge of [first, middle) and [middle, last), optionally using
/// uninitialized scratch `buffer` of `bufLen` elements (spare container
/// capacity). With scratch and movelib available, dispatches to
/// boost::movelib::adaptive_merge; falls back to std::inplace_merge.
template <comparator_erasure Erasure = comparator_erasure::never, std::random_access_iterator It, typename Comparator>
constexpr void inplace_merge(
    It const first, It const middle, It const last,
    Comparator const & __restrict comp,
    std::iter_value_t<It> * const buffer = nullptr, std::size_t const bufLen = 0
) noexcept
{
    using key_t = std::iter_value_t<It>;
#if PSI_VM_HAS_ADAPTIVE_MERGE
    if constexpr ( std::contiguous_iterator<It> )
    {
        if !consteval
        {
            if constexpr ( Erasure == comparator_erasure::allowed && detail::erasure_applies<Comparator, key_t> )
            {
                detail::erased_adaptive_merge<key_t>(
                    std::to_address( first ), std::to_address( middle ), std::to_address( last ),
                    erased_ref_predicate<key_t>::bind( comp ), buffer, bufLen );
            }
            else
            {
                boost::movelib::adaptive_merge(
                    std::to_address( first ), std::to_address( middle ), std::to_address( last ),
                    make_trivially_copyable_predicate( comp ), buffer, bufLen );
            }
            return;
        }
    }
#endif // PSI_VM_HAS_ADAPTIVE_MERGE
    std::inplace_merge( first, middle, last, make_trivially_copyable_predicate( comp ) );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
