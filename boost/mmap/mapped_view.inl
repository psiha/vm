////////////////////////////////////////////////////////////////////////////////
///
/// \file mapped_view.inl
/// ---------------------
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
#include "mapped_view.hpp"

#include "detail/impl_inline.hpp"

#include "boost/assert.hpp"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif // WIN32_LEAN_AND_MEAN
    #include "windows.h"

    #pragma warning ( disable : 4996 ) // "The POSIX name for this item is deprecated. Instead, use the ISO C++ conformant name."
    #include "io.h"
#else
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


BOOST_IMPL_INLINE
mapping_flags mapping_flags::create
(
    unsigned int const handle_access_flags,
    unsigned int const share_mode         ,
    unsigned int const system_hints
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


template <> BOOST_IMPL_INLINE
mapped_view_reference<unsigned char> mapped_view_reference<unsigned char>::map
(
    native_handle::reference         const object_handle,
    mapping_flags            const &       flags,
    std::size_t                      const desired_size,
    std::size_t                      const offset
)
{
    typedef mapped_view_reference<unsigned char>::iterator iterator_t;

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
    handle<win32> const mapping
    (
        ::CreateFileMapping( object_handle, 0, flags.create_mapping_flags, large_integer.HighPart, large_integer.LowPart, 0 )
    );
    BOOST_ASSERT
    (
        !mapping || ( object_handle == INVALID_HANDLE_VALUE ) || ( desired_size != 0 )
    );

    large_integer.QuadPart = offset;
    iterator_t const view_start( static_cast<iterator_t>( ::MapViewOfFile( mapping.get(), flags.map_view_flags, large_integer.HighPart, large_integer.LowPart, desired_size ) ) );
    return mapped_view_reference<unsigned char>
    (
        view_start,
        ( view_start && ( object_handle != INVALID_HANDLE_VALUE ) )
            ? view_start + desired_size
            : view_start
    );

#else // POSIX

    iterator_t const view_start( static_cast<iterator_t>( ::mmap( 0, desired_size, flags.protection, flags.flags, object_handle, 0 ) ) );
    return mapped_view_reference<unsigned char>
    (
        view_start,
        ( view_start != MAP_FAILED )
            ? view_start + desired_size
            : view_start
    );

#endif // OS API
}


template <> BOOST_IMPL_INLINE
void detail::mapped_view_base<unsigned char const>::unmap( detail::mapped_view_base<unsigned char const> const & mapped_range )
{
#ifdef _WIN32
    BOOST_VERIFY( ::UnmapViewOfFile(                              mapped_range.begin()                        )        || mapped_range.empty() );
#else
    BOOST_VERIFY( ( ::munmap       ( const_cast<unsigned char *>( mapped_range.begin() ), mapped_range.size() ) == 0 ) || mapped_range.empty() );
#endif // _WIN32
}


template <> BOOST_IMPL_INLINE
mapped_view_reference<unsigned char const> mapped_view_reference<unsigned char const>::map
(
    native_handle::reference const object_handle,
    std::size_t              const desired_size,
    std::size_t              const offset,
    bool                     const map_for_code_execution
)
{
    return mapped_view_reference<unsigned char>::map
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


BOOST_IMPL_INLINE
basic_mapped_view_ref map_file( char const * const file_name, std::size_t desired_size )
{
    typedef native_file_flags file_flags;
    native_handle const file_handle
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
        set_size( file_handle.get(), desired_size );
    else
        desired_size = get_size( file_handle.get() );

    return basic_mapped_view_ref::map
    (
        file_handle.get(),
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


BOOST_IMPL_INLINE
basic_mapped_read_only_view_ref map_read_only_file( char const * const file_name )
{
    typedef native_file_flags file_flags;
    native_handle const file_handle
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

    return basic_mapped_read_only_view_ref::map
    (
        file_handle.get(),
        // Implementation note:
        //   Windows APIs interpret zero as 'whole file' but we still need to
        // query the file size in order to be able to properly set the end
        // pointer.
        //                                    (13.07.2011.) (Domagoj Saric)
        get_size( file_handle.get() )
    );
}


//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
