////////////////////////////////////////////////////////////////////////////////
///
/// \file handle.inl
/// ----------------
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
#include "handle.hpp"

#include "flags.hpp"
#include "../../detail/impl_inline.hpp"

#include "boost/assert.hpp"

#ifdef BOOST_MSVC
    #pragma warning ( disable : 4996 ) // "The POSIX name for this item is deprecated. Instead, use the ISO C++ conformant name."
    #include "io.h"
#else
    #include "sys/mman.h"      // mmap, munmap.
    #include "sys/stat.h"
    #include "sys/types.h"     // struct stat.
    #include "unistd.h"        // sysconf.
#endif // BOOST_MSVC
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
namespace guard
{

BOOST_IMPL_INLINE
posix_handle::posix_handle( handle_t const handle )
    :
    handle_( handle )
{}

#ifdef BOOST_MSVC
BOOST_IMPL_INLINE
posix_handle::posix_handle( windows_handle::handle_t const native_handle )
    :
    handle_( ::_open_osfhandle( reinterpret_cast<intptr_t>( native_handle ), _O_APPEND ) )
{
    if ( handle_ == -1 )
    {
        BOOST_VERIFY
        (
            ( ::CloseHandle( native_handle ) != false                     ) ||
            ( native_handle == 0 || native_handle == INVALID_HANDLE_VALUE )
        );
    }
}
#endif // BOOST_MSVC

BOOST_IMPL_INLINE
posix_handle::~posix_handle()
{
    BOOST_VERIFY
    (
        ( ::close( handle() ) == 0 ) ||
        (
            ( handle() == -1    ) &&
            ( errno    == EBADF )
        )
    );                
}

//------------------------------------------------------------------------------
} // guard


BOOST_IMPL_INLINE
guard::posix_handle create_file( char const * const file_name, posix_file_flags const & flags )
{
    BOOST_ASSERT( file_name );

    int const current_mask( ::umask( 0 ) );
    int const file_handle ( ::open( file_name, flags.oflag, flags.pmode ) );
    //...zzz...investigate posix_fadvise, posix_madvise, fcntl for the system hints...
    BOOST_VERIFY( ::umask( current_mask ) == 0 );

    return guard::posix_handle( file_handle );
}


#ifndef BOOST_MSVC
BOOST_IMPL_INLINE
bool set_file_size( guard::posix_handle::handle_t const file_handle, std::size_t const desired_size )
{
    return ::ftruncate( file_handle, desired_size ) != -1;
}
#endif // BOOST_MSVC


BOOST_IMPL_INLINE
std::size_t get_file_size( guard::posix_handle::handle_t const file_handle )
{
    struct stat file_info;
    BOOST_VERIFY( ::fstat( file_handle, &file_info ) == 0 );
    return file_info.st_size;
}

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
