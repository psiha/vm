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
#include <psi/vm/mappable_objects/file/file.hpp>

#include <psi/vm/detail/posix.hpp>
#include <psi/vm/flags/opening.posix.hpp>
#include <psi/vm/mapping/mapping.posix.hpp>

#include <boost/assert.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

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
    struct stat file_info{ .st_size = 0 }; // ensure zero is returned for invalid handles (in unchecked/release builds)
    BOOST_VERIFY( ( ::fstat( file_handle.value, &file_info ) == 0 ) || ( file_handle == handle_traits::invalid_value ) );
    return static_cast< std::uint64_t >( file_info.st_size );
}

//------------------------------------------------------------------------------
} // posix
//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
