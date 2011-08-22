////////////////////////////////////////////////////////////////////////////////
///
/// \file handle.hpp
/// ----------------
///
/// Copyright (c) Domagoj Saric 2011.
///
///  Use, modification and distribution is subject to the Boost Software License, Version 1.0.
///  (See accompanying file LICENSE_1_0.txt or copy at
///  http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef handle_hpp__67A36E06_53FF_4361_9786_9E04A3917CD3
#define handle_hpp__67A36E06_53FF_4361_9786_9E04A3917CD3
#pragma once
//------------------------------------------------------------------------------
#include "../detail/impl_selection.hpp"

#include BOOST_MMAP_IMPL_INCLUDE( ., handle.hpp )
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
#endif // handle_hpp
