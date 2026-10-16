////////////////////////////////////////////////////////////////////////////////
/// Utility union for easier debugging: no need for special 'visualizers' over
/// type-erased byte arrays -- contained values are directly visible in the
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
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// Unconstructed elements must not be destroyed. A trivial element needs no
// such suppression, and the defaulted destructor is then trivial.
template <typename T>
consteval bool suppress_uninitialized_destructor() noexcept {
    if constexpr ( requires { sizeof( T ); } ) { return !std::is_trivially_destructible_v<T>; }
    else { return true; }
}

template <typename T, std::uint32_t size>
union [[ clang::trivial_abi ]] noninitialized_array
{
    constexpr  noninitialized_array() noexcept {}
    constexpr ~noninitialized_array() noexcept
        requires ( suppress_uninitialized_destructor<T>() )
    {}
    constexpr ~noninitialized_array() noexcept = default;

    T data[ size ];
}; // noninitialized_array

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
