////////////////////////////////////////////////////////////////////////////////
/// small_vector<T,N> -- inline (stack) buffer with heap spill.
///
/// Alias over vector<sbo_hybrid<T,N,sz_t,options>>.
/// Layout and growth controlled by sbo_options (see storage/sbo_hybrid.hpp).
///
/// free_small_vector<T,sz_t> picks the inline capacity that fits in what the
/// heap-only vector already spends (see free_inline_capacity() below).
///
/// Copyright (c) Domagoj Saric.
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once

#include <psi/vm/containers/heap_vector.hpp>
#include <psi/vm/containers/storage/sbo_hybrid.hpp>
#include <psi/vm/containers/vector.hpp>

#include <cstdint>

//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// sbo_layout, sbo_options, sbo_hybrid, resolve_layout
// are defined in storage/sbo_hybrid.hpp.

template <typename T, std::uint32_t N, typename sz_t = std::uint32_t, sbo_options options = {}>
using small_vector = vector<sbo_hybrid<T, N, sz_t, options>, options.growth>;


namespace detail
{
    template <typename T, typename sz_t, sbo_options options, std::uint32_t N>
    [[ nodiscard ]] consteval std::uint32_t largest_free_inline_capacity() noexcept
    {
        if constexpr ( N == 0 )
        {
            return 0;
        }
        else if constexpr ( sizeof( small_vector<T, N, sz_t, options> ) <= sizeof( heap_vector<T, sz_t> ) )
        {
            // Measuring the budget but asserting the identity: a footprint
            // *below* the heap-only one would mean the two representations no
            // longer share a layout, which is a finding, not a bonus.
            static_assert
            (
                sizeof( small_vector<T, N, sz_t, options> ) == sizeof( heap_vector<T, sz_t> ),
                "the fitted inline capacity no longer reproduces the heap-only footprint"
            );
            return N;
        }
        else
        {
            return largest_free_inline_capacity<T, sz_t, options, N - 1>();
        }
    }

    template <typename T, typename sz_t, sbo_options options, std::uint32_t Capacity>
    struct free_sbo
    {
        static_assert
        (
            Capacity != 0,
            "no inline capacity fits within the heap-only footprint for this element and size type: "
            "narrow sz_t, leave the layout on auto_select, or name an explicit N to accept the larger object"
        );
        // The substitute keeps this alias well formed, so the assertion above
        // is the whole diagnostic rather than the head of a cascade.
        using type = small_vector<T, Capacity ? Capacity : 1, sz_t, options>;
    };
} // namespace detail

//! Largest N for which small_vector<T,N,sz_t,options> still fits inside
//! sizeof( heap_vector<T,sz_t> ) -- i.e. the inline capacity that the
//! heap-only representation already pays for. 0 when even one element does
//! not fit: an element that alone overruns the budget, or an explicitly
//! requested layout that keeps the size outside the union (which costs a word
//! at every N, and is the whole slack of a pointer + size + capacity object).
template <typename T, typename sz_t = std::uint32_t, sbo_options options = {}>
[[ nodiscard ]] consteval std::uint32_t free_inline_capacity() noexcept
{
    // N elements occupy at least N * sizeof( T ), so nothing above this can
    // fit; sizeof() never shrinks as N grows, so the first fit found scanning
    // down is the largest one.
    auto constexpr upper_bound{ static_cast<std::uint32_t>( sizeof( heap_vector<T, sz_t> ) / sizeof( T ) ) };
    return detail::largest_free_inline_capacity<T, sz_t, options, upper_bound>();
}

//! small_vector sized to free_inline_capacity<T,sz_t,options>(). Ill-formed
//! when that is 0 -- silently falling back to N=1 would hand back a larger
//! object under a name that promises none.
template <typename T, typename sz_t = std::uint32_t, sbo_options options = {}>
using free_small_vector = typename detail::free_sbo<T, sz_t, options, free_inline_capacity<T, sz_t, options>()>::type;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
