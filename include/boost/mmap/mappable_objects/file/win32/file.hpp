////////////////////////////////////////////////////////////////////////////////
///
/// \file win32/file.hpp
/// --------------------
///
/// Copyright (c) Domagoj Saric 2010 - 2015.
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
#ifndef file_hpp__FB482005_18D9_4E3B_9193_A13DBFE88F45
#define file_hpp__FB482005_18D9_4E3B_9193_A13DBFE88F45
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/detail/impl_selection.hpp"
#include "boost/mmap/flags/win32/opening.hpp"
#include "boost/mmap/mapping/mapping.hpp"
#include "boost/mmap/mappable_objects/file/handle.hpp"
#include "boost/mmap/error/error.hpp"

#include <cstddef>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
namespace win32
{
//------------------------------------------------------------------------------

template <typename> struct is_resizable;
template <        > struct is_resizable<file_handle> : std::true_type {};

file_handle BOOST_CC_REG create_file( char    const * file_name, flags::opening ) noexcept;
file_handle BOOST_CC_REG create_file( wchar_t const * file_name, flags::opening ) noexcept;
bool        BOOST_CC_REG delete_file( char    const * file_name                 ) noexcept;
bool        BOOST_CC_REG delete_file( wchar_t const * file_name                 ) noexcept;


bool        BOOST_CC_REG set_size( file_handle::reference, std::size_t desired_size ) noexcept;
std::size_t BOOST_CC_REG get_size( file_handle::reference                           ) noexcept;

// https://msdn.microsoft.com/en-us/library/ms810613.aspx Managing Memory-Mapped Files

mapping BOOST_CC_REG create_mapping
(
    handle::reference,
    flags::access_privileges::object,
    flags::access_privileges::child_process,
    flags::mapping          ::share_mode,
    std  ::size_t size
) noexcept;

//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "file.inl"
#endif // BOOST_MMAP_HEADER_ONLY

#endif // file_hpp
