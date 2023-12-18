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

#include "../allocation.hpp"
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------

namespace // internal header
{
    constexpr void       * add( void       * const ptr, auto const diff ) noexcept { return static_cast<std::byte       *>( ptr ) + diff; }
    constexpr void const * add( void const * const ptr, auto const diff ) noexcept { return static_cast<std::byte const *>( ptr ) + diff; }
    constexpr auto         add( auto         const x  , auto const diff ) noexcept { return                                 x     + diff; }

    constexpr auto         sub( auto         const x  , auto const diff ) noexcept { return                                 x     - diff; }
} // anonymous namespace

//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------
