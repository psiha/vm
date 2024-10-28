////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2023 - 2024.
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

namespace // internal header
{
    constexpr void add( void       * & ptr, auto const diff ) noexcept { reinterpret_cast<std::intptr_t &>( ptr ) += diff; }
    constexpr void add( void const * & ptr, auto const diff ) noexcept { reinterpret_cast<std::intptr_t &>( ptr ) += diff; }
    constexpr void add( auto         & x  , auto const diff ) noexcept {                                    x     += diff; }

    constexpr void sub( auto         & x  , auto const diff ) noexcept {                                    x     -= diff; }
} // anonymous namespace

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
