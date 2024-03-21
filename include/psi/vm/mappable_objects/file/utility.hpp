////////////////////////////////////////////////////////////////////////////////
///
/// \file utility.hpp
/// -----------------
///
/// Copyright (c) Domagoj Saric 2010 - 2024.
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

#include <psi/vm/error/error.hpp>
#include <psi/vm/flags/opening.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>

#include <psi/err/fallible_result.hpp>

#include <cstddef>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

#ifdef _MSC_VER
#   pragma warning( push )
#   pragma warning( disable : 5030 ) // Unrecognized attribute
#endif // _MSC_VER

[[ gnu::const ]] flags::opening create_rw_file_flags( flags::named_object_construction_policy policy = flags::named_object_construction_policy::open_or_create ) noexcept;
[[ gnu::const ]] flags::opening create_r_file_flags () noexcept;

err::fallible_result<mapped_view          , error> map_file          (    char const * file_name, std::size_t desired_size ) noexcept;
err::fallible_result<read_only_mapped_view, error> map_read_only_file(    char const * file_name                           ) noexcept;

#ifdef _WIN32
err::fallible_result<mapped_view          , error> map_file          ( wchar_t const * file_name, std::size_t desired_size ) noexcept;
err::fallible_result<read_only_mapped_view, error> map_read_only_file( wchar_t const * file_name                           ) noexcept;
#endif // _WIN32

#ifdef _MSC_VER
#   pragma warning( pop )
#endif // _MSC_VER

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
