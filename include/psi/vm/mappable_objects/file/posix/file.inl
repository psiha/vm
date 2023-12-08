////////////////////////////////////////////////////////////////////////////////
///
/// \file mappable_objects/file/posix/file.inl
/// ------------------------------------------
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
#ifndef file_inl__1E2F9841_1C6C_40D9_9AA7_BAC0003CD909
#define file_inl__1E2F9841_1C6C_40D9_9AA7_BAC0003CD909
#pragma once
//------------------------------------------------------------------------------
#include "file.hpp"

#include "psi/vm/detail/impl_inline.hpp"
#include "psi/vm/detail/posix.hpp"
#include "psi/vm/flags/posix/opening.hpp"
#include "psi/vm/mapping/posix/mapping.hpp"

#include <boost/assert.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cerrno>
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------
inline namespace posix
{
//------------------------------------------------------------------------------

PSI_VM_IMPL_INLINE
file_handle BOOST_CC_REG create_file( char const * const file_name, flags::opening const flags ) noexcept
{
    BOOST_ASSERT( file_name );
    // POSIX specific - flags.pmode gets umasked - however this cannot be
    // automatically overridden locally in a thread-safe manner
    // http://man7.org/linux/man-pages/man2/umask.2.html
    int const c_file_handle( ::open( file_name, flags.oflag, static_cast< mode_t >( flags.pmode ) ) );
    //...zzz...investigate posix_fadvise_madvise, fcntl for the system hints...
    return file_handle( c_file_handle );
}

#ifdef BOOST_MSVC
PSI_VM_IMPL_INLINE
file_handle BOOST_CC_REG create_file( wchar_t const * const file_name, flags::opening const flags )
{
    BOOST_ASSERT( file_name );
    int const file_handle( ::_wopen( file_name, flags.oflag, flags.pmode ) );
    return file_handle( file_handle );
}
#endif // BOOST_MSVC


PSI_VM_IMPL_INLINE bool BOOST_CC_REG delete_file(    char const * const file_name ) noexcept { return ::  unlink( file_name ) == 0; }
#ifdef BOOST_MSVC
PSI_VM_IMPL_INLINE bool BOOST_CC_REG delete_file( wchar_t const * const file_name ) noexcept { return ::_wunlink( file_name ) == 0; }
#endif // BOOST_MSVC


#ifdef BOOST_HAS_UNISTD_H
PSI_VM_IMPL_INLINE bool BOOST_CC_REG set_size( file_handle::reference const file_handle, std::size_t const desired_size ) noexcept { return ::ftruncate( file_handle, static_cast< off_t >( desired_size ) ) != -1; }
#endif // BOOST_HAS_UNISTD_H

PSI_VM_IMPL_INLINE
std::size_t BOOST_CC_REG get_size( file_handle::reference const file_handle ) noexcept
{
    struct stat file_info;
    BOOST_VERIFY( ( ::fstat( file_handle, &file_info ) == 0 ) || ( file_handle == handle_traits::invalid_value ) );
    return static_cast< std::size_t >( file_info.st_size );
}

//------------------------------------------------------------------------------
} // posix
//------------------------------------------------------------------------------
} // vm
//------------------------------------------------------------------------------
} // psi
//------------------------------------------------------------------------------
#endif // file_inl
