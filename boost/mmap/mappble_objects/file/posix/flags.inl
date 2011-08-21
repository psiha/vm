////////////////////////////////////////////////////////////////////////////////
///
/// \file flags.inl
/// ---------------
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
#include "flags.hpp"

#include "../../../detail/impl_inline.hpp"

#include "boost/assert.hpp"

#ifdef _WIN32
    #define BOOST_AUX_IO_WIN32_OR_POSIX( win32, posix ) win32
    #pragma warning ( disable : 4996 ) // "The POSIX name for this item is deprecated. Instead, use the ISO C++ conformant name."
    #include "io.h"
    #include "sys/stat.h"
#else
    #define BOOST_AUX_IO_WIN32_OR_POSIX( win32, posix ) posix
    #include "sys/mman.h"      // mmap, munmap.
    #include "sys/stat.h"
    #include "sys/types.h"     // struct stat.
    #include "unistd.h"        // sysconf.
#endif // _WIN32
#include "errno.h"
#include "fcntl.h"

#ifndef _WIN32
    #ifdef __APPLE__
        #define BOOST_AUX_MMAP_POSIX_OR_OSX( posix, osx ) osx
    #else
        #define BOOST_AUX_MMAP_POSIX_OR_OSX( posix, osx ) posix
    #endif
#endif
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

unsigned int const posix_file_flags::handle_access_rights::read    = O_RDONLY;
unsigned int const posix_file_flags::handle_access_rights::write   = O_WRONLY;
unsigned int const posix_file_flags::handle_access_rights::execute = O_RDONLY;

unsigned int const posix_file_flags::share_mode::none   = 0;
unsigned int const posix_file_flags::share_mode::read   = 0;
unsigned int const posix_file_flags::share_mode::write  = 0;
unsigned int const posix_file_flags::share_mode::remove = 0;

unsigned int const posix_file_flags::system_hints::random_access     = BOOST_AUX_IO_WIN32_OR_POSIX( O_RANDOM     , 0 );
unsigned int const posix_file_flags::system_hints::sequential_access = BOOST_AUX_IO_WIN32_OR_POSIX( O_SEQUENTIAL , 0 );
// http://stackoverflow.com/questions/2299402/how-does-one-do-raw-io-on-mac-os-x-ie-equivalent-to-linuxs-o-direct-flag
unsigned int const posix_file_flags::system_hints::non_cached        = BOOST_AUX_IO_WIN32_OR_POSIX( 0            , BOOST_AUX_MMAP_POSIX_OR_OSX( O_DIRECT, 0 ) );
unsigned int const posix_file_flags::system_hints::delete_on_close   = BOOST_AUX_IO_WIN32_OR_POSIX( O_TEMPORARY  , 0 );
unsigned int const posix_file_flags::system_hints::temporary         = BOOST_AUX_IO_WIN32_OR_POSIX( _O_SHORT_LIVED, 0 );

unsigned int const posix_file_flags::on_construction_rights::read    = BOOST_AUX_IO_WIN32_OR_POSIX( _S_IREAD , S_IRUSR );
unsigned int const posix_file_flags::on_construction_rights::write   = BOOST_AUX_IO_WIN32_OR_POSIX( _S_IWRITE, S_IWUSR );
unsigned int const posix_file_flags::on_construction_rights::execute = BOOST_AUX_IO_WIN32_OR_POSIX( _S_IEXEC , S_IXUSR );

BOOST_IMPL_INLINE
posix_file_flags posix_file_flags::create
(
    unsigned int  const handle_access_flags   ,
    unsigned int  const /*share_mode*/        ,
    open_policy_t const open_flags            ,
    unsigned int  const system_hints          ,
    unsigned int  const on_construction_rights
)
{
    posix_file_flags const flags =
    {
        ( ( handle_access_flags == ( O_RDONLY | O_WRONLY ) ) ? O_RDWR : handle_access_flags )
            |
        open_flags
            |
        system_hints, // oflag
        on_construction_rights // pmode
    };

    return flags;
}


BOOST_IMPL_INLINE
posix_file_flags posix_file_flags::create_for_opening_existing_files( unsigned int const handle_access_flags, unsigned int const share_mode , bool const truncate, unsigned int const system_hints )
{
    return create
    (
        handle_access_flags,
        share_mode,
        truncate
            ? open_policy::open_and_truncate_existing
            : open_policy::open_existing,
        system_hints,
        0
    );
}


//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
