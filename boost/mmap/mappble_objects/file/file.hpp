////////////////////////////////////////////////////////////////////////////////
///
/// \file file.hpp
/// --------------
///
/// Copyright (c) Domagoj Saric 2010.-2011.
///
///  Use, modification and distribution is subject to the Boost Software License, Version 1.0.
///  (See accompanying file LICENSE_1_0.txt or copy at
///  http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef file_hpp__D3705ED0_EC0D_4747_A789_1EE17252B6E2
#define file_hpp__D3705ED0_EC0D_4747_A789_1EE17252B6E2
#pragma once
//------------------------------------------------------------------------------
#ifdef _WIN32
    #include "win32/file.hpp"
    #include "win32/flags.hpp"
#else
    #include "posix/file.hpp"
    #include "posix/flags.hpp"
#endif
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

#ifdef _WIN32
    typedef windows_handle   native_handle;
    typedef win32_file_flags native_file_flags;
#else
    typedef posix_handle     native_handle;
    typedef posix_file_flags native_file_flags;
#endif // _WIN32
typedef native_handle::handle_t native_handle_t;

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // file_hpp
