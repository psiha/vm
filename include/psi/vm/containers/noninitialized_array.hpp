////////////////////////////////////////////////////////////////////////////////
/// Utility union for easier debugging: no need for special 'visualizers' over
/// type-erased byte arrays — contained values are directly visible in the
/// debugger.
////////////////////////////////////////////////////////////////////////////////
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

#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

template <typename T, std::uint32_t size>
union [[ clang::trivial_abi ]] noninitialized_array
{
    constexpr  noninitialized_array() noexcept {}
    constexpr ~noninitialized_array() noexcept {}

    T data[ size ];
}; // noninitialized_array

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
