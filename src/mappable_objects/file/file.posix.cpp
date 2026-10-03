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
#if defined( __linux__ ) && !defined( _GNU_SOURCE )
#   define _GNU_SOURCE // fallocate() is a GNU extension
#endif

#include <psi/vm/mappable_objects/file/file.hpp>

#if __has_include( <unistd.h> )
#include <psi/vm/align.hpp>
#include <psi/vm/allocation.hpp> // for reserve_granularity
#endif
#include <psi/vm/detail/posix.hpp>
#include <psi/vm/flags/opening.posix.hpp>
#include <psi/vm/mapping/mapping.posix.hpp>

#include <boost/assert.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>

//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
PSI_VM_POSIX_INLINE namespace posix
{
//------------------------------------------------------------------------------

file_handle create_file( char const * const file_name, flags::opening const flags ) noexcept
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
file_handle create_file( wchar_t const * const file_name, flags::opening const flags )
{
    BOOST_ASSERT( file_name );
    int const file_handle( ::_wopen( file_name, flags.oflag, flags.pmode ) );
    return file_handle( file_handle );
}
#endif // BOOST_MSVC


bool delete_file(    char const * const file_name ) noexcept { return ::  unlink( file_name ) == 0; }
#ifdef BOOST_MSVC
bool delete_file( wchar_t const * const file_name ) noexcept { return ::_wunlink( file_name ) == 0; }
#endif // BOOST_MSVC


#if __has_include( <unistd.h> )
err::fallible_result<void, error> set_size( file_handle::reference const file_handle, std::uint64_t const desired_size ) noexcept
{
    if ( ::ftruncate( file_handle, static_cast<off_t>( desired_size ) ) == 0 ) [[ likely ]]
        return err::success;
    return error{};
}
#endif // POSIX impl level

std::uint64_t get_size( file_handle::const_reference const file_handle ) noexcept
{
#if defined( __clang__ ) || defined( __GNUC__ )
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
    struct stat file_info{ .st_size = 0 }; // ensure zero is returned for invalid handles (in unchecked/release builds)
    BOOST_VERIFY( ( ::fstat( file_handle.value, &file_info ) == 0 ) || ( file_handle == handle_traits::invalid_value ) );
    return static_cast<std::uint64_t>( file_info.st_size );
#if defined( __clang__ ) || defined( __GNUC__ )
#   pragma GCC diagnostic pop
#endif
}

bool punch_hole( file_handle::reference const file_handle, std::uint64_t const offset, std::uint64_t length ) noexcept
{
    auto const size{ get_size( file_handle ) };
    if ( offset >= size || length == 0 )
        return true; // nothing to deallocate: the (clamped) range is empty
    length = std::min( length, size - offset );
#if defined( __linux__ ) && defined( FALLOC_FL_PUNCH_HOLE )
    return ::fallocate( file_handle, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, static_cast<off_t>( offset ), static_cast<off_t>( length ) ) == 0;
#elif defined( __APPLE__ ) && defined( F_PUNCHHOLE )
    // F_PUNCHHOLE requires block aligned arguments: punch the aligned interior
    // and zero the (less than a block long) edges with ordinary writes.
    struct stat info;
    if ( ::fstat( file_handle, &info ) != 0 || info.st_blksize <= 0 )
        return false;
    auto const block{ static_cast<std::uint64_t>( info.st_blksize ) };
    auto const end  { offset + length };
    auto const first{ ( offset + block - 1 ) / block * block }; // align_up
    auto const last { end / block * block                    }; // align_down

    auto const zero = [ & ]( std::uint64_t from, std::uint64_t const to ) noexcept
    {
        char const zeros[ 4096 ]{};
        while ( from < to )
        {
            auto const chunk{ std::min<std::uint64_t>( to - from, sizeof( zeros ) ) };
            auto const written{ ::pwrite( file_handle, zeros, static_cast<std::size_t>( chunk ), static_cast<off_t>( from ) ) };
            if ( written <= 0 )
                return false;
            from += static_cast<std::uint64_t>( written );
        }
        return true;
    };

    if ( first >= last ) // no whole block inside the range
        return zero( offset, end );
    // Punch first: a filesystem without support must leave the file unchanged.
    fpunchhole_t hole{ .fp_flags = 0, .reserved = 0, .fp_offset = static_cast<off_t>( first ), .fp_length = static_cast<off_t>( last - first ) };
    if ( ::fcntl( file_handle, F_PUNCHHOLE, &hole ) != 0 )
        return false;
    return zero( offset, first ) && zero( last, end );
#else
    return false;
#endif
}

#if __has_include( <unistd.h> )
mapping create_mapping
(
    handle                                  &&       file,
    flags::access_privileges::object           const object_access,
    flags::access_privileges::child_process    const child_access,
    flags::mapping          ::share_mode       const share_mode,
    std::size_t                                      size
) noexcept
{
    // Apple guidelines http://developer.apple.com/library/mac/#documentation/Performance/Conceptual/FileSystem/Articles/MappingFiles.html
    (void)child_access; //...mrmlj...figure out what to do with this...
    auto view_flags{ flags::viewing::create( object_access, share_mode ) };
    if ( !file )
    {
        // emulate the Windows interface: null file signifies that the user
        // wants a temporary/non-persisted 'anonymous'/pagefile-backed mapping
        // TODO a separate function for this purpose
#    if defined( MAP_SHARED_VALIDATE )
        // mmap fails with EINVAL (under WSL kernel 5.15 w/ ArchLinux) when
        // MAP_SHARED_VALIDATE is combined with MAP_ANONYMOUS
        if ( ( view_flags.flags & MAP_TYPE ) == MAP_SHARED_VALIDATE )
        {
            view_flags.flags &= ~MAP_SHARED_VALIDATE;
            view_flags.flags |=  MAP_SHARED;
        }
#    endif
        view_flags.flags |= MAP_ANONYMOUS;
        size              = align_up( size, reserve_granularity );
    }
    return { std::move( file ), view_flags, size };
}

// mapping_posix.cpp (not yet existent file) contents//////////////////////////
err::fallible_result<void, error> set_size( mapping & mapping, std::size_t const desired_size ) noexcept
{
    if ( mapping.is_anonymous() )
    {
        mapping.maximum_size = align_up( desired_size, reserve_granularity );
        return err::success;
    }
    auto result{ set_size( mapping.underlying_file(), desired_size )() };
    if ( result )
        mapping.maximum_size = align_up( desired_size, reserve_granularity );
    return result;
}
std::size_t get_size( mapping const & mapping ) noexcept
{
    if ( mapping.is_anonymous() )
        return mapping.maximum_size;
    // TODO update or verify&return mapping.maximum_size?
    return static_cast<std::size_t>( get_size( mapping.underlying_file() ) );
}
///////////////////////////////////////////////////////////////////////////////

#endif // POSIX impl level

//------------------------------------------------------------------------------
} // posix
//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
