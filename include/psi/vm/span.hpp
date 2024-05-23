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

#include <cstddef>

#include <span>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// Intentional strong typedef (to catch broken usage with 'random' user spans at compile time)
struct           mapped_span : std::span<std::byte      > { using std::span<std::byte      >::span; explicit           mapped_span( std::span<std::byte      > const src ) noexcept : std::span<std::byte      >( src ) {} };
struct read_only_mapped_span : std::span<std::byte const> { using std::span<std::byte const>::span; explicit read_only_mapped_span( std::span<std::byte const> const src ) noexcept : std::span<std::byte const>( src ) {} };

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
