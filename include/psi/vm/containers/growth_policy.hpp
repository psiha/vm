////////////////////////////////////////////////////////////////////////////////
///
/// \file growth_policy.hpp
/// -----------------------
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

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

struct geometric_growth
{
    std::uint8_t num{ 3 }; // numerator   (3/2 = 1.5x default)
    std::uint8_t den{ 2 }; // denominator (num == den -> no geometric growth)

    /// Returns max(target_size, current_capacity * num / den), evaluated in a
    /// wide enough type and saturated at T's maximum.
    /// The intermediate product must not be computed in T: for a 32-bit
    /// size_type it overflows once the capacity passes max/num, after which the
    /// wrapped (small) result loses to target_size and growth silently
    /// degenerates to exact fit - turning append-heavy use quadratic exactly
    /// where the buffers, and so the reallocation copies, are largest.
    template <std::unsigned_integral T>
    [[ nodiscard ]] constexpr T operator()( T const target_size, T const current_capacity ) const noexcept
    {
        using wide_t = std::common_type_t<T, std::uint64_t>;
        auto const grown{ static_cast<wide_t>( current_capacity ) * num / den };
        return std::max( target_size, static_cast<T>( std::min<wide_t>( grown, std::numeric_limits<T>::max() ) ) );
    }

    [[ nodiscard ]] explicit constexpr operator bool() const noexcept { return num != den; }
};

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
