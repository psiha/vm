////////////////////////////////////////////////////////////////////////////////
///
/// \file handle.hpp
/// ----------------
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
#ifndef file_hpp__1E2F9841_1C6C_40D9_9AA7_BAC0003CD909
#define file_hpp__1E2F9841_1C6C_40D9_9AA7_BAC0003CD909
#pragma once
//------------------------------------------------------------------------------
#include "../handle.hpp"
#include "../../../detail/posix.hpp"
#include "../../../implementations.hpp"

#include <cstddef>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl  > struct file_open_flags;
template <typename Impl  > struct file_mapping_flags;
template <class    Handle> struct is_resizable;

#ifdef BOOST_HAS_UNISTD_H
    template <> struct is_resizable<handle<posix>> : std::true_type  {};
#else
    template <> struct is_resizable<handle<posix>> : std::false_type {};
#endif // BOOST_HAS_UNISTD_H


file_handle<posix> BOOST_CC_REG create_file( char    const * file_name, file_open_flags<posix> ) noexcept;
#ifdef BOOST_MSVC
file_handle<posix> BOOST_CC_REG create_file( wchar_t const * file_name, file_open_flags<posix> ) noexcept;
#endif // BOOST_MSVC

bool BOOST_CC_REG delete_file( char    const * file_name, posix ) noexcept;
bool BOOST_CC_REG delete_file( wchar_t const * file_name, posix ) noexcept;


#ifdef BOOST_HAS_UNISTD_H
bool        BOOST_CC_REG set_size( file_handle<posix>::reference, std::size_t desired_size ) noexcept;
#endif // BOOST_HAS_UNISTD_H
std::size_t BOOST_CC_REG get_size( file_handle<posix>::reference                           ) noexcept;

mapping<posix> BOOST_CC_REG create_mapping( file_handle<posix>::reference, file_mapping_flags<posix> ) noexcept;

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "file.inl"
#endif

#endif // file_hpp
