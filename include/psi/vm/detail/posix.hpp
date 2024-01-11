////////////////////////////////////////////////////////////////////////////////
///
/// \file posix.hpp
/// ---------------
///
/// Copyright (c) Domagoj Saric 2011 - 2024.
///
///  Use, modification and distribution is subject to the Boost Software License, Version 1.0.
///  (See accompanying file LICENSE_1_0.txt or copy at
///  http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once

#if __has_include( <unistd.h> )
#   include <boost/config/detail/posix_features.hpp>
#   include <cstddef>
#   include <cstdint>
#elif defined( _MSC_VER )
#   pragma warning ( disable : 4996 ) // "The POSIX name for this item is deprecated. Instead, use the ISO C++ conformant name."
#   include <io.h>
#   include <wchar.h>
#else
#   error no suitable POSIX implementation found
#endif // impl

#if defined( _MSC_VER )
#   define PSI_VM_POSIX_STANDARD_LINUX_OSX_MSVC( standard, linux, osx, msvc ) msvc
#elif defined( __APPLE__ )
#   define PSI_VM_POSIX_STANDARD_LINUX_OSX_MSVC( standard, linux, osx, msvc ) osx
#elif defined( _GNU_SOURCE )
#   define PSI_VM_POSIX_STANDARD_LINUX_OSX_MSVC( standard, linux, osx, msvc ) linux
#else
#   define PSI_VM_POSIX_STANDARD_LINUX_OSX_MSVC( standard, linux, osx, msvc ) standard
#endif // POSIX impl
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
#ifndef _WIN32 //...mrmlj...
inline namespace posix
{
    void * mmap( void * target_address, std::size_t size, int protection, int flags, int file_handle, std::uint64_t offset ) noexcept;
}
#endif
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
