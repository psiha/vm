////////////////////////////////////////////////////////////////////////////////
///
/// \file file.inl
/// --------------
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
#include "file.hpp"

#include "flags.hpp"
#include "../../detail/impl_inline.hpp"

#include "boost/assert.hpp"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#include "windows.h"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

BOOST_IMPL_INLINE
handle<win32> create_file( char const * const file_name, file_flags<win32> const & flags )
{
    BOOST_ASSERT( file_name );

    HANDLE const file_handle
    (
        ::CreateFileA
        (
            file_name, flags.desired_access, flags.share_mode, 0, flags.creation_disposition, flags.flags_and_attributes, 0
        )
    );
    BOOST_ASSERT( ( file_handle == INVALID_HANDLE_VALUE ) || ( ::GetLastError() == NO_ERROR ) || ( ::GetLastError() == ERROR_ALREADY_EXISTS ) );

    return handle<win32>( file_handle );
}


BOOST_IMPL_INLINE
bool set_size( handle<win32>::reference const file_handle, std::size_t const desired_size )
{
    // It is 'OK' to send null/invalid handles to Windows functions (they will
    // simply fail), this simplifies error handling (it is enough to go through
    // all the logic, inspect the final result and then throw on error).
    #ifdef _WIN64
        BOOST_VERIFY
        (
            ::SetFilePointerEx( file_handle, reinterpret_cast<LARGE_INTEGER const &>( desired_size ), NULL, FILE_BEGIN ) ||
            ( file_handle == INVALID_HANDLE_VALUE )
        );
    #else // _WIN32/64
        DWORD const new_size( ::SetFilePointer( file_handle, desired_size, NULL, FILE_BEGIN ) );
        BOOST_ASSERT( ( new_size == desired_size ) || ( file_handle == INVALID_HANDLE_VALUE ) );
        ignore_unused_variable_warning( new_size );
    #endif // _WIN32/64

    BOOL const success( ::SetEndOfFile( file_handle ) );

    #ifdef _WIN64
        BOOST_VERIFY
        (
            ::SetFilePointerEx( file_handle, 0, NULL, FILE_BEGIN ) ||
            ( file_handle == INVALID_HANDLE_VALUE )
        );
    #else // _WIN32/64
        BOOST_VERIFY( ( ::SetFilePointer( file_handle, 0, NULL, FILE_BEGIN ) == 0 ) || ( file_handle == INVALID_HANDLE_VALUE ) );
    #endif // _WIN32/64

    return success != false;
}


BOOST_IMPL_INLINE
std::size_t get_size( handle<win32>::reference const file_handle )
{
    #ifdef _WIN64
        LARGE_INTEGER file_size;
        BOOST_VERIFY( ::GetFileSizeEx( file_handle, &file_size ) || ( file_handle == INVALID_HANDLE_VALUE ) );
        return file_size.QuadPart;
    #else // _WIN32/64
        DWORD const file_size( ::GetFileSize( file_handle, 0 ) );
        BOOST_ASSERT( ( file_size != INVALID_FILE_SIZE ) || ( file_handle == INVALID_HANDLE_VALUE ) || ( ::GetLastError() == NO_ERROR ) );
        return file_size;
    #endif // _WIN32/64
}

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
