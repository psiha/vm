////////////////////////////////////////////////////////////////////////////////
///
/// \file mappable_objects/file/posix/file.inl
/// ------------------------------------------
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
#ifndef file_inl__1E2F9841_1C6C_40D9_9AA7_BAC0003CD909
#define file_inl__1E2F9841_1C6C_40D9_9AA7_BAC0003CD909
#pragma once
//------------------------------------------------------------------------------
#include "file.hpp"

#include "boost/mmap/detail/impl_inline.hpp"
#include "boost/mmap/detail/posix.hpp"
#include "boost/mmap/flags/posix/opening.hpp"
#include "boost/mmap/mapping/posix/mapping.hpp"

#include "boost/assert.hpp"

#include "fcntl.h"
#include "sys/stat.h"
#include "sys/types.h"

#include <cerrno>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

BOOST_IMPL_INLINE
file_handle<posix> BOOST_CC_REG create_file( char const * const file_name, flags::opening<posix> const flags ) noexcept
{
    BOOST_ASSERT( file_name );

    int const current_mask ( ::umask( 0 ) );
    int const c_file_handle( ::open( file_name, flags.oflag, flags.pmode ) );
    //...zzz...investigate posix_fadvise, posix_madvise, fcntl for the system hints...
    BOOST_VERIFY( ::umask( current_mask ) == 0 );

    return file_handle<posix>( c_file_handle );
}

#ifdef BOOST_MSVC
BOOST_IMPL_INLINE
file_handle<posix> BOOST_CC_REG create_file( wchar_t const * const file_name, flags::opening<posix> const flags )
{
    BOOST_ASSERT( file_name );

    int const current_mask( ::umask( 0 ) );
    int const file_handle ( ::_wopen( file_name, flags.oflag, flags.pmode ) );
    BOOST_VERIFY( ::umask( current_mask ) == 0 );

    return file_handle<posix>( file_handle );
}
#endif // BOOST_MSVC


BOOST_IMPL_INLINE bool BOOST_CC_REG delete_file(    char const * const file_name, posix ) noexcept { return ::  unlink( file_name ) == 0; }
#ifdef BOOST_MSVC
BOOST_IMPL_INLINE bool BOOST_CC_REG delete_file( wchar_t const * const file_name, posix ) noexcept { return ::_wunlink( file_name ) == 0; }
#endif // BOOST_MSVC


#ifdef BOOST_HAS_UNISTD_H
BOOST_IMPL_INLINE bool BOOST_CC_REG set_size( file_handle<posix>::reference const file_handle, std::size_t const desired_size ) noexcept { return ::ftruncate( file_handle, desired_size ) != -1; }
#endif // BOOST_HAS_UNISTD_H

BOOST_IMPL_INLINE
std::size_t BOOST_CC_REG get_size( file_handle<posix>::reference const file_handle ) noexcept
{
    struct stat file_info;
    BOOST_VERIFY( ( ::fstat( file_handle, &file_info ) == 0 ) || ( file_handle == handle_traits<posix>::invalid_value ) );
    return file_info.st_size;
}

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------

#endif // file_inl
