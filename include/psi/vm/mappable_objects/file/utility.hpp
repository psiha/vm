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
#ifndef utility_hpp__3713A8AF_A516_4A23_BE6A_2BB79EBF7B5F
#define utility_hpp__3713A8AF_A516_4A23_BE6A_2BB79EBF7B5F
#pragma once
//------------------------------------------------------------------------------
#include "psi/vm/mapped_view/mapped_view.hpp"
#include "psi/vm/error/error.hpp"

#include "boost/err/fallible_result.hpp"

#include <cstddef>
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------

err::fallible_result<mapped_view          , error> map_file          (    char const * file_name, std::size_t desired_size ) noexcept;
err::fallible_result<read_only_mapped_view, error> map_read_only_file(    char const * file_name                           ) noexcept;

#ifdef _WIN32
err::fallible_result<mapped_view          , error> map_file          ( wchar_t const * file_name, std::size_t desired_size ) noexcept;
err::fallible_result<read_only_mapped_view, error> map_read_only_file( wchar_t const * file_name                           ) noexcept;
#endif // _WIN32

//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------

#ifdef PSI_VM_HEADER_ONLY
    #include "utility.inl"
#endif // PSI_VM_HEADER_ONLY

#endif // utility_hpp