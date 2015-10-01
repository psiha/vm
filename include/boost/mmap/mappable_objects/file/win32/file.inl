////////////////////////////////////////////////////////////////////////////////
///
/// \file file.inl
/// --------------
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
#ifndef file_inl__FB482005_18D9_4E3B_9193_A13DBFE88F45
#define file_inl__FB482005_18D9_4E3B_9193_A13DBFE88F45
#pragma once
//------------------------------------------------------------------------------
#include "file.hpp"

#include "boost/mmap/flags/win32/opening.hpp"
#include "boost/mmap/detail/impl_inline.hpp"
#include "boost/mmap/detail/nt.hpp"
#include "boost/mmap/detail/win32.hpp"

#include "boost/assert.hpp"
#include "boost/err/win32.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

// http://en.wikipedia.org/wiki/File_locking#In_UNIX
DWORD const default_unix_shared_semantics( FILE_SHARE_READ | FILE_SHARE_WRITE );

namespace detail
{
    struct create_file
    {
        template <typename ... T> static BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE HANDLE call_create( char    const * __restrict const file_name, T const ... args ) { return CreateFileA( file_name, args... ); }
        template <typename ... T> static BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE HANDLE call_create( wchar_t const * __restrict const file_name, T const ... args ) { return CreateFileW( file_name, args... ); }

        template <typename char_t>
        BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE
        static HANDLE BOOST_CC_REG do_create( char_t const * __restrict const file_name, flags::opening<win32> const & __restrict flags ) noexcept
        {
            ::SECURITY_ATTRIBUTES sa;
            auto const p_security_attributes( flags::detail::make_sa_ptr( sa, flags.ap.system_access.p_sd, reinterpret_cast<bool const &>/*static_cast<bool>*/( flags.ap.child_access ) ) );
            auto const handle
            (
                call_create
                (
                    file_name, flags.ap.object_access.privileges, default_unix_shared_semantics, const_cast<LPSECURITY_ATTRIBUTES>( p_security_attributes ), static_cast<DWORD>( flags.creation_disposition.value ), flags.flags_and_attributes, nullptr
                )
            );
            BOOST_ASSERT( ( handle == handle_traits<win32>::invalid_value ) || ( ::GetLastError() == NO_ERROR ) || ( ::GetLastError() == ERROR_ALREADY_EXISTS ) );

            return handle;
        }
    }; // struct create_file
} // namespace detail

BOOST_IMPL_INLINE BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 )
file_handle<win32> BOOST_CC_REG create_file( char    const * const file_name, flags::opening<win32> const flags ) noexcept { return file_handle<win32>{ detail::create_file::do_create( file_name, flags ) }; }
BOOST_IMPL_INLINE
file_handle<win32> BOOST_CC_REG create_file( wchar_t const * const file_name, flags::opening<win32> const flags ) noexcept { return file_handle<win32>{ detail::create_file::do_create( file_name, flags ) }; }


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
    DWORD const file_size( ::GetFileSize( file_handle, nullptr ) );
    BOOST_ASSERT( ( file_size != INVALID_FILE_SIZE ) || ( file_handle == handle_traits<win32>::invalid_value ) || ( ::GetLastError() == NO_ERROR ) );
    return file_size;
#endif // _WIN32/64
}


namespace detail
{
    inline
    std::size_t get_section_size( HANDLE const mapping_handle )
    {
        detail::SECTION_BASIC_INFORMATION info;
        auto const result( detail::NtQuerySection( mapping_handle, detail::SECTION_INFORMATION_CLASS::SectionBasicInformation, &info, sizeof( info ), nullptr ) );
        BOOST_ASSERT( NT_SUCCESS( result ) );
        return static_cast<std::size_t>( info.SectionSize.QuadPart );
    }

    // https://support.microsoft.com/en-us/kb/125713 Common File Mapping Problems and Platform Differences
    struct create_mapping_impl
    {
        template <typename ... T> static BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE HANDLE call_create( char    const * __restrict const file_name, T const ... args ) { return ::CreateFileMappingA( args..., file_name ); }
        template <typename ... T> static BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE HANDLE call_create( wchar_t const * __restrict const file_name, T const ... args ) { return ::CreateFileMappingW( args..., file_name ); }

        template <typename ... T> static BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE HANDLE call_open  ( char    const * __restrict const file_name, T const ... args ) { return ::OpenFileMappingA  ( args..., file_name ); }
        template <typename ... T> static BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE HANDLE call_open  ( wchar_t const * __restrict const file_name, T const ... args ) { return ::OpenFileMappingW  ( args..., file_name ); }

        static BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 )/* BOOST_FORCEINLINE*/
        HANDLE map_file( handle<win32>::reference const file, flags::flags_t const flags, std::uint64_t const size )
        {
            auto const & sz( reinterpret_cast<ULARGE_INTEGER const &>( size ) );
            return call_create( static_cast<wchar_t const *>( nullptr ), file, nullptr, flags, sz.HighPart, sz.LowPart );
        }
        static
        HANDLE map_file( handle<win32>::reference const file, flags::flags_t const flags ) { return map_file( file, flags, 0 ); }

        static void clear( HANDLE & mapping_handle )
        {
            handle<win32>::traits::close( mapping_handle );
            mapping_handle = nullptr;
        }

        template <typename char_t>
        BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE
        static mapping<win32> BOOST_CC_REG do_map( file_handle<win32>::reference const file, flags::mapping<win32> const flags, std::uint64_t const maximum_size, char_t const * __restrict const name ) noexcept
        {
            ::SECURITY_ATTRIBUTES sa;
            auto const p_security_attributes( flags::detail::make_sa_ptr( sa, flags.system_access.p_sd, reinterpret_cast<bool const &>/*static_cast<bool>*/( flags.child_access ) ) );
            auto const max_sz               ( reinterpret_cast<ULARGE_INTEGER const &>( maximum_size ) );
            auto /*const*/ mapping_handle
            (
                call_create( name, file, const_cast<::LPSECURITY_ATTRIBUTES>( p_security_attributes ), flags.create_mapping_flags, max_sz.HighPart, max_sz.LowPart )
            );
            BOOST_ASSERT_MSG
            (
                ( file != handle_traits<win32>::invalid_value ) || maximum_size,
                "CreateFileMapping accepts INVALID_HANDLE_VALUE as valid input but only "
                "if the size parameter is not zero."
            );
            auto const creation_disposition( flags.creation_disposition.value );
            auto const error               ( err::last_win32_error::get()     );
            auto const preexisting         ( error == ERROR_ALREADY_EXISTS    );
            using disposition = flags::named_object_construction_policy<win32>;
            switch ( creation_disposition )
            {
                case disposition::open_existing                  : if ( !preexisting ) clear( mapping_handle );
                case disposition::open_or_create                 : break;

                case disposition::create_new_or_truncate_existing:
                    if ( preexisting )
                    {
                        if ( get_section_size( mapping_handle ) != maximum_size )
                            clear( mapping_handle );
                    }
                    break;

                case disposition::open_and_truncate_existing     : if ( !preexisting || get_section_size( mapping_handle ) != maximum_size ) clear( mapping_handle ); break;
                case disposition::create_new                     : if (  preexisting )                                                       clear( mapping_handle ); break;
            }

            return { mapping_handle, flags.map_view_flags };
        }
    }; // struct create_mapping_impl
} // namespace detail

BOOST_IMPL_INLINE
mapping<win32> BOOST_CC_REG create_mapping( file_handle<win32>::reference const file, flags::mapping<win32> const flags, std::uint64_t const maximum_size, char const * const name ) noexcept
{
    return detail::create_mapping_impl::do_map( file, flags, maximum_size, name );
}

#if 0
BOOST_IMPL_INLINE
mapping<win32> BOOST_CC_REG create_mapping( handle<win32>::reference const file, flags::mapping<win32> const flags ) noexcept
{
    auto const mapping_handle
    (
        detail::create_mapping_impl::map_file( file, flags.create_mapping_flags )
    );
    return { mapping_handle, flags.map_view_flags };
}
#endif

BOOST_IMPL_INLINE
mapping<win32> BOOST_CC_REG create_mapping
(
    handle                  <win32>::reference     const file,
    flags::access_privileges<win32>::object        const object_access,
    flags::access_privileges<win32>::child_process const child_access,
    flags::mapping          <win32>::share_mode    const share_mode,
    std::size_t                                    const size
) noexcept
{
    auto const mapping_handle
    (
        detail::create_mapping_impl::map_file( file, flags::detail::object_access_to_page_access( object_access, share_mode ), size )
    );
    return { mapping_handle, flags::viewing<win32>::create( object_access, share_mode ) };
}

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
#endif // file_inl
