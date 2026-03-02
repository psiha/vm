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
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

struct geometric_growth
{
    std::uint8_t num{ 3 }; // numerator   (3/2 = 1.5x default)
    std::uint8_t den{ 2 }; // denominator (num == den -> no geometric growth)

    /// Returns max(target_size, current_capacity * num / den).
    template <std::unsigned_integral T>
    [[ nodiscard ]] constexpr T operator()( T const target_size, T const current_capacity ) const noexcept { return std::max( target_size, static_cast<T>( current_capacity * num / den ) ); }

    [[ nodiscard ]] explicit constexpr operator bool() const noexcept { return num != den; }
};

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
