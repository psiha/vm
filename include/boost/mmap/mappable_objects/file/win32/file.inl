////////////////////////////////////////////////////////////////////////////////
///
/// \file file.inl
/// --------------
///
/// Copyright (c) Domagoj Saric 2010¸- 2015.
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
#ifndef file_inl__FB482005_18D9_4E3B_9193_A13DBFE88F45
#define file_inl__FB482005_18D9_4E3B_9193_A13DBFE88F45
#pragma once
//------------------------------------------------------------------------------
#include "file.hpp"

#include "open_flags.hpp"
#include "boost/mmap/detail/impl_inline.hpp"
#include "boost/mmap/detail/win32.hpp"

#include "boost/assert.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

namespace
{
    // http://en.wikipedia.org/wiki/File_locking#In_UNIX
    DWORD const default_unix_shared_semantics( FILE_SHARE_READ | FILE_SHARE_WRITE );
} // namespace

BOOST_IMPL_INLINE
file_handle<win32> BOOST_CC_REG create_file( char const * const file_name, file_open_flags<win32> const flags ) noexcept
{
    auto const handle
    (
        ::CreateFileA
        (
            file_name, flags.desired_access, default_unix_shared_semantics, nullptr, flags.creation_disposition, flags.flags_and_attributes, nullptr
        )
    );
    BOOST_ASSERT( ( handle == handle_traits<win32>::invalid_value ) || ( ::GetLastError() == NO_ERROR ) || ( ::GetLastError() == ERROR_ALREADY_EXISTS ) );
    
    return file_handle<win32>( handle );
}

BOOST_IMPL_INLINE
file_handle<win32> BOOST_CC_REG create_file( wchar_t const * const file_name, file_open_flags<win32> const flags ) noexcept
{
    BOOST_ASSERT( file_name );

    auto const handle
    (
        ::CreateFileW
        (
            file_name, flags.desired_access, default_unix_shared_semantics, nullptr, flags.creation_disposition, flags.flags_and_attributes, nullptr
        )
    );
    BOOST_ASSERT( ( handle == handle_traits<win32>::invalid_value ) || ( ::GetLastError() == NO_ERROR ) || ( ::GetLastError() == ERROR_ALREADY_EXISTS ) );
    
    return file_handle<win32>( handle );
}


BOOST_IMPL_INLINE bool BOOST_CC_REG delete_file( char    const * const file_name, win32 ) noexcept { return ::DeleteFileA( file_name ) != false; }
BOOST_IMPL_INLINE bool BOOST_CC_REG delete_file( wchar_t const * const file_name, win32 ) noexcept { return ::DeleteFileW( file_name ) != false; }


BOOST_IMPL_INLINE
bool BOOST_CC_REG set_size( file_handle<win32>::reference const file_handle, std::size_t const desired_size ) noexcept
{
    // It is 'OK' to send null/invalid handles to Windows functions (they will
    // simply fail), this simplifies error handling (it is enough to go through
    // all the logic, inspect the final result and then 'throw' on error).
#ifdef _WIN64
    BOOST_VERIFY
    (
        ::SetFilePointerEx( file_handle, reinterpret_cast<LARGE_INTEGER const &>( desired_size ), nullptr, FILE_BEGIN ) ||
        ( file_handle == handle_traits<win32>::invalid_value )
    );
#else // _WIN32/64
    DWORD const new_size( ::SetFilePointer( file_handle, desired_size, nullptr, FILE_BEGIN ) );
    BOOST_ASSERT( ( new_size == desired_size ) || ( file_handle == handle_traits<win32>::invalid_value ) );
    ignore_unused_variable_warning( new_size );
#endif // _WIN32/64

    BOOL const success( ::SetEndOfFile( file_handle ) );

#ifdef _WIN64
    LARGE_INTEGER const offset = { 0 };
    BOOST_VERIFY
    (
        ::SetFilePointerEx( file_handle, offset, nullptr, FILE_BEGIN ) ||
        ( file_handle == handle_traits<win32>::invalid_value )
    );
#else // _WIN32/64
    BOOST_VERIFY( ( ::SetFilePointer( file_handle, 0, nullptr, FILE_BEGIN ) == 0 ) || ( file_handle == handle_traits<win32>::invalid_value ) );
#endif // _WIN32/64

    return success != false;
}


BOOST_IMPL_INLINE
std::size_t BOOST_CC_REG get_size( file_handle<win32>::reference const file_handle ) noexcept
{
#ifdef _WIN64
    LARGE_INTEGER file_size;
    BOOST_VERIFY( ::GetFileSizeEx( file_handle, &file_size ) || ( file_handle == handle_traits<win32>::invalid_value ) );
    return file_size.QuadPart;
#else // _WIN32/64
    DWORD const file_size( ::GetFileSize( file_handle, 0 ) );
    BOOST_ASSERT( ( file_size != INVALID_FILE_SIZE ) || ( file_handle == handle_traits<win32>::invalid_value ) || ( ::GetLastError() == NO_ERROR ) );
    return file_size;
#endif // _WIN32/64
}


BOOST_IMPL_INLINE
mapping<win32> BOOST_CC_REG create_mapping( file_handle<win32>::reference const file, file_mapping_flags<win32> const flags, std::uint64_t const maximum_size, char const * const name ) noexcept
{
    auto const & max_sz( reinterpret_cast<ULARGE_INTEGER const &>( maximum_size ) );
    auto const mapping_handle
    (
        ::CreateFileMappingA( file, static_cast<::SECURITY_ATTRIBUTES *>( const_cast<void *>( flags.p_security_attributes ) ), flags.create_mapping_flags, max_sz.HighPart, max_sz.LowPart, name )
    );
    BOOST_ASSERT_MSG
    (
        ( file != handle_traits<win32>::invalid_value ) || !mapping_handle,
        "CreateFileMapping accepts INVALID_HANDLE_VALUE as valid input but only "
        "if the size parameter is not zero."
    );
    return mapping<win32>( mapping_handle, flags.map_view_flags );
}

// https://support.microsoft.com/en-us/kb/125713 Common File Mapping Problems and Platform Differences

BOOST_IMPL_INLINE
mapping<win32> BOOST_CC_REG create_mapping( handle<win32>::reference const file, file_mapping_flags<win32> const flags ) noexcept
{
    auto const mapping_handle
    (
        ::CreateFileMappingW( file, nullptr, flags.create_mapping_flags, 0, 0, nullptr )
    );
    BOOST_ASSERT_MSG
    (
        ( file != handle_traits<win32>::invalid_value ) || !mapping_handle,
        "CreateFileMapping accepts INVALID_HANDLE_VALUE as valid input but only "
        "if the size parameter is not zero."
    );
    return mapping<win32>( mapping_handle, flags.map_view_flags );
}

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
#endif // file_inl
