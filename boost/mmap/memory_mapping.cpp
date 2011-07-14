////////////////////////////////////////////////////////////////////////////////
///
/// \file memory_mapping.cpp
/// ------------------------
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
#include "memory_mapping.hpp"

#include "boost/assert.hpp"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif // WIN32_LEAN_AND_MEAN
    #include "windows.h"

    #pragma warning ( disable : 4996 ) // "The POSIX name for this item is deprecated. Instead, use the ISO C++ conformant name."
    #include "io.h"
#else
    #include <sys/mman.h>      // mmap, munmap.
    #include <sys/stat.h>
    #include <sys/types.h>     // struct stat.
    #include <unistd.h>        // sysconf.
#endif // _WIN32
#include <errno.h>
#include <fcntl.h>

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
namespace guard
{

#ifdef _WIN32
windows_handle::windows_handle( handle_t const handle )
    :
    handle_( handle )
{}

windows_handle::~windows_handle()
{
    BOOST_VERIFY
    (
        ( ::CloseHandle( handle_ ) != false               ) ||
        ( handle_ == 0 || handle_ == INVALID_HANDLE_VALUE )
    );
}

windows_handle::handle_t const & windows_handle::handle() const
{
    return handle_;
}
#endif // _WIN32

posix_handle::posix_handle( handle_t const handle )
    :
    handle_( handle )
{}

#ifdef _WIN32
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
#endif // _WIN32

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


posix_handle::handle_t const & posix_handle::handle() const
{
    return handle_;
}

//------------------------------------------------------------------------------
} // guard


guard::native_handle create_file( char const * const file_name, file_flags const & flags )
{
    BOOST_ASSERT( file_name );

#ifdef _WIN32

    HANDLE const file_handle
    (
        ::CreateFileA
        (
            file_name, flags.desired_access, flags.share_mode, 0, flags.creation_disposition, flags.flags_and_attributes, 0
        )
    );
    BOOST_ASSERT( ( file_handle == INVALID_HANDLE_VALUE ) || ( ::GetLastError() == NO_ERROR ) || ( ::GetLastError() == ERROR_ALREADY_EXISTS ) );

#else

    mode_t const current_mask( ::umask( 0 ) );
    int    const file_handle ( ::open( file_name, flags.oflag, flags.pmode ) );
    //...zzz...investigate posix_fadvise, posix_madvise, fcntl for the system hints...
    BOOST_VERIFY( ::umask( current_mask ) == 0 );

#endif // _WIN32

    return guard::native_handle( file_handle );
}


bool set_file_size( guard::native_handle_t const file_handle, std::size_t const desired_size )
{
#ifdef _WIN32
    // It is 'OK' to send null/invalid handles to Windows functions (they will
    // simply fail), this simplifies error handling (it is enough to go through
    // all the logic, inspect the final result and then throw on error).
    DWORD const new_size( ::SetFilePointer( file_handle, desired_size, 0, FILE_BEGIN ) );
    BOOST_ASSERT( ( new_size == desired_size ) || ( file_handle == INVALID_HANDLE_VALUE ) );
    ignore_unused_variable_warning( new_size );

    BOOL const success( ::SetEndOfFile( file_handle ) );

    BOOST_VERIFY( ( ::SetFilePointer( file_handle, 0, 0, FILE_BEGIN ) == 0 ) || ( file_handle == INVALID_HANDLE_VALUE ) );

    return success != false;
#else
    return ::ftruncate( file_handle, desired_size ) != -1;
#endif // _WIN32
}


std::size_t get_file_size( guard::native_handle_t const file_handle )
{
#ifdef _WIN32
    DWORD const file_size( ::GetFileSize( file_handle, 0 ) );
    BOOST_ASSERT( ( file_size != INVALID_FILE_SIZE ) || ( file_handle == INVALID_HANDLE_VALUE ) || ( ::GetLastError() == NO_ERROR ) );
    return file_size;
#else
    struct stat file_info;
    BOOST_VERIFY( ::fstat( file_handle, &file_info ) == 0 );
    return file_info.st_size;
#endif // _WIN32
}


unsigned int const file_flags::handle_access_rights::read    = BOOST_AUX_IO_WIN32_OR_POSIX( GENERIC_READ   , O_RDONLY );
unsigned int const file_flags::handle_access_rights::write   = BOOST_AUX_IO_WIN32_OR_POSIX( GENERIC_WRITE  , O_WRONLY );
unsigned int const file_flags::handle_access_rights::execute = BOOST_AUX_IO_WIN32_OR_POSIX( GENERIC_EXECUTE, O_RDONLY );

unsigned int const file_flags::share_mode::none   = BOOST_AUX_IO_WIN32_OR_POSIX( 0                , 0 );
unsigned int const file_flags::share_mode::read   = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_SHARE_READ  , 0 );
unsigned int const file_flags::share_mode::write  = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_SHARE_WRITE , 0 );
unsigned int const file_flags::share_mode::remove = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_SHARE_DELETE, 0 );

unsigned int const file_flags::system_hints::random_access     = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_FLAG_RANDOM_ACCESS                         , /*O_RANDOM*/0      );//...zzz...msvc specific flags...fix this...
unsigned int const file_flags::system_hints::sequential_access = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_FLAG_SEQUENTIAL_SCAN                       , /*O_SEQUENTIAL*/0  );
unsigned int const file_flags::system_hints::non_cached        = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, /*O_DIRECT*/0      );
unsigned int const file_flags::system_hints::delete_on_close   = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_FLAG_DELETE_ON_CLOSE                       , /*O_TEMPORARY*/0   );
unsigned int const file_flags::system_hints::temporary         = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_ATTRIBUTE_TEMPORARY                        , /*O_SHORT_LIVED*/0 );

unsigned int const file_flags::on_construction_rights::read    = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_ATTRIBUTE_READONLY, S_IRUSR );
unsigned int const file_flags::on_construction_rights::write   = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_ATTRIBUTE_NORMAL  , S_IWUSR );
unsigned int const file_flags::on_construction_rights::execute = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_ATTRIBUTE_NORMAL  , S_IXUSR );

file_flags file_flags::create
(
    unsigned int  const handle_access_flags   ,
    unsigned int  const share_mode            ,
    open_policy_t const open_flags            ,
    unsigned int  const system_hints          ,
    unsigned int  const on_construction_rights
)
{
    file_flags const flags =
    {
    #ifdef _WIN32
        handle_access_flags, // desired_access
        share_mode, // share_mode
        open_flags, // creation_disposition
        system_hints
            |
        (
            ( on_construction_rights & FILE_ATTRIBUTE_NORMAL )
                ? ( on_construction_rights & ~FILE_ATTRIBUTE_READONLY )
                :   on_construction_rights
        ) // flags_and_attributes
    #else // POSIX
        ( ( handle_access_flags == ( O_RDONLY | O_WRONLY ) ) ? O_RDWR : handle_access_flags )
            |
        open_flags
            |
        system_hints, // oflag
        on_construction_rights // pmode
    #endif // OS impl
    };

    return flags;
}


file_flags file_flags::create_for_opening_existing_files( unsigned int const handle_access_flags, unsigned int const share_mode , bool const truncate, unsigned int const system_hints )
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


unsigned int const mapping_flags::handle_access_rights::read    = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_MAP_READ   , PROT_READ  );
unsigned int const mapping_flags::handle_access_rights::write   = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_MAP_WRITE  , PROT_WRITE );
unsigned int const mapping_flags::handle_access_rights::execute = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_MAP_EXECUTE, PROT_EXEC  );

unsigned int const mapping_flags::share_mode::shared = BOOST_AUX_IO_WIN32_OR_POSIX(             0, MAP_SHARED  );
unsigned int const mapping_flags::share_mode::hidden = BOOST_AUX_IO_WIN32_OR_POSIX( FILE_MAP_COPY, MAP_PRIVATE );

unsigned int const mapping_flags::system_hint::strict_target_address   = BOOST_AUX_IO_WIN32_OR_POSIX(           0, MAP_FIXED              );
unsigned int const mapping_flags::system_hint::lock_to_ram             = BOOST_AUX_IO_WIN32_OR_POSIX( SEC_COMMIT , BOOST_AUX_MMAP_POSIX_OR_OSX( MAP_LOCKED, 0 )             );
unsigned int const mapping_flags::system_hint::reserve_page_file_space = BOOST_AUX_IO_WIN32_OR_POSIX( SEC_RESERVE, /*khm#1*/MAP_NORESERVE );
unsigned int const mapping_flags::system_hint::precommit               = BOOST_AUX_IO_WIN32_OR_POSIX( SEC_COMMIT , BOOST_AUX_MMAP_POSIX_OR_OSX( MAP_POPULATE, 0 )           );
unsigned int const mapping_flags::system_hint::uninitialized           = BOOST_AUX_IO_WIN32_OR_POSIX(           0, BOOST_AUX_MMAP_POSIX_OR_OSX( MAP_UNINITIALIZED, 0 )      );


mapping_flags mapping_flags::create
(
    unsigned int handle_access_flags,
    unsigned int share_mode         ,
    unsigned int system_hints
)
{
    mapping_flags flags;
#ifdef _WIN32
    flags.create_mapping_flags = ( handle_access_flags & handle_access_rights::execute ) ? PAGE_EXECUTE : PAGE_NOACCESS;
    if ( share_mode == share_mode::hidden ) // WRITECOPY
        flags.create_mapping_flags *= 8;
    else
    if ( handle_access_flags & handle_access_rights::write )
        flags.create_mapping_flags *= 4;
    else
    {
        BOOST_ASSERT( handle_access_flags & handle_access_rights::read );
        flags.create_mapping_flags *= 2;
    }

    flags.create_mapping_flags |= system_hints;

    flags.map_view_flags        = handle_access_flags;
#else
    flags.protection = handle_access_flags;
    flags.flags      = share_mode | system_hints;
    if ( ( system_hints & system_hint::reserve_page_file_space ) ) /*khm#1*/
        flags.flags &= ~MAP_NORESERVE;
    else
        flags.flags |= MAP_NORESERVE;
#endif // _WIN32

    return flags;
}


template <>
mapped_view<unsigned char> mapped_view<unsigned char>::map
(
    guard::native_handle_t const   object_handle,
    mapping_flags          const & flags,
    std::size_t            const   desired_size,
    std::size_t            const   offset
)
{
    typedef mapped_view<unsigned char>::iterator iterator_t;

#ifdef _WIN32

    // Implementation note:
    // Mapped views hold internal references to the following handles so we do
    // not need to hold/store them ourselves:
    // http://msdn.microsoft.com/en-us/library/aa366537(VS.85).aspx
    //                                        (26.03.2010.) (Domagoj Saric)

    ULARGE_INTEGER large_integer;

    // CreateFileMapping accepts INVALID_HANDLE_VALUE as valid input but only if
    // the size parameter is not null.
    large_integer.QuadPart = desired_size;
    guard::windows_handle const mapping
    (
        ::CreateFileMapping( object_handle, 0, flags.create_mapping_flags, large_integer.HighPart, large_integer.LowPart, 0 )
    );
    BOOST_ASSERT
    (
        !mapping.handle() || ( object_handle == INVALID_HANDLE_VALUE ) || ( desired_size != 0 )
    );

    large_integer.QuadPart = offset;
    iterator_t const view_start( static_cast<iterator_t>( ::MapViewOfFile( mapping.handle(), flags.map_view_flags, large_integer.HighPart, large_integer.LowPart, desired_size ) ) );
    return mapped_view<unsigned char>
    (
        view_start,
        ( view_start && ( object_handle != INVALID_HANDLE_VALUE ) )
            ? view_start + desired_size
            : view_start
    );

#else // POSIX

    iterator_t const view_start( static_cast<iterator_t>( ::mmap( 0, desired_size, flags.protection, flags.flags, object_handle, 0 ) ) );
    return mapped_view<unsigned char>
    (
        view_start,
        ( view_start != MAP_FAILED )
            ? view_start + desired_size
            : view_start
    );

#endif // OS API
}

template <>
void detail::mapped_view_base<unsigned char const>::unmap( detail::mapped_view_base<unsigned char const> const & mapped_range )
{
#ifdef _WIN32
    BOOST_VERIFY( ::UnmapViewOfFile(                              mapped_range.begin()                        )        || mapped_range.empty() );
#else
    BOOST_VERIFY( ( ::munmap       ( const_cast<unsigned char *>( mapped_range.begin() ), mapped_range.size() ) == 0 ) || mapped_range.empty() );
#endif // _WIN32
}

template <>
mapped_view<unsigned char const> mapped_view<unsigned char const>::map
(
    guard::native_handle_t const object_handle,
    std::size_t            const desired_size,
    std::size_t            const offset,
    bool                   const map_for_code_execution
)
{
    return mapped_view<unsigned char>::map
    (
        object_handle,
        mapping_flags::create
        (
            mapping_flags::handle_access_rights::read | ( map_for_code_execution ? mapping_flags::handle_access_rights::execute : 0 ),
            mapping_flags::share_mode::shared,
            mapping_flags::system_hint::uninitialized
        ),
        desired_size,
        offset
    );
}


basic_mapped_view map_file( char const * const file_name, std::size_t desired_size )
{
    guard::native_handle const file_handle
    (
        create_file
        (
            file_name,
            file_flags::create
            (
                file_flags::handle_access_rights::read | file_flags::handle_access_rights::write,
                file_flags::share_mode          ::read,
                file_flags::open_policy::open_or_create,
                file_flags::system_hints        ::sequential_access,
                file_flags::on_construction_rights::read | file_flags::on_construction_rights::write
            )
        )
    );

    if ( desired_size )
        set_file_size( file_handle.handle(), desired_size );
    else
        desired_size = get_file_size( file_handle.handle() );

    return basic_mapped_view::map
    (
        file_handle.handle(),
        mapping_flags::create
        (
            mapping_flags::handle_access_rights::read | mapping_flags::handle_access_rights::write,
            mapping_flags::share_mode::shared,
            mapping_flags::system_hint::uninitialized
        ),
        desired_size,
        0
    );
}


basic_mapped_read_only_view map_read_only_file( char const * const file_name )
{
    guard::native_handle const file_handle
    (
        create_file
        (
            file_name,
            file_flags::create_for_opening_existing_files
            (
                file_flags::handle_access_rights::read,
                file_flags::share_mode          ::read | file_flags::share_mode::write,
                false,
                file_flags::system_hints        ::sequential_access
            )
        )
    );

    return basic_mapped_read_only_view::map
    (
        file_handle.handle(),
        // Implementation note:
        //   Windows APIs interpret zero as 'whole file' but we still need to
        // query the file size in order to be able properly set the end pointer.
        //                                    (13.07.2011.) (Domagoj Saric)
        get_file_size( file_handle.handle() )
    );
}


//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
