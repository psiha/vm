////////////////////////////////////////////////////////////////////////////////
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

#include <psi/vm/flags/opening.win32.hpp>
#include <psi/vm/detail/nt.hpp>
#include <psi/vm/detail/win32.hpp>

#include <boost/assert.hpp>

#include <psi/err/win32.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace win32
{
//------------------------------------------------------------------------------

// http://en.wikipedia.org/wiki/File_locking#In_UNIX
DWORD const default_unix_shared_semantics{ FILE_SHARE_READ | FILE_SHARE_WRITE };

namespace detail
{
    struct create_file
    {
        static BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE HANDLE call_create( char    const * __restrict const file_name, auto const ... args ) { return CreateFileA( file_name, args... ); }
        static BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE HANDLE call_create( wchar_t const * __restrict const file_name, auto const ... args ) { return CreateFileW( file_name, args... ); }

        BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE
        static HANDLE do_create( auto const * __restrict const file_name, flags::opening const & __restrict flags ) noexcept
        {
            ::SECURITY_ATTRIBUTES sa;
            auto const p_security_attributes( flags::detail::make_sa_ptr( sa, flags.ap.system_access.p_sd, reinterpret_cast<bool const &>/*static_cast<bool>*/( flags.ap.child_access ) ) );
            auto handle
            (
                call_create
                (
                    file_name, flags.ap.object_access.privileges, default_unix_shared_semantics, const_cast<LPSECURITY_ATTRIBUTES>( p_security_attributes ), static_cast<DWORD>( flags.creation_disposition ), flags.flags_and_attributes, nullptr
                )
            );
            BOOST_ASSERT( ( handle == INVALID_HANDLE_VALUE ) || ( ::GetLastError() == NO_ERROR ) || ( ::GetLastError() == ERROR_ALREADY_EXISTS ) );
            BOOST_ASSUME( handle != nullptr );
            if ( ( file_handle::invalid_value != INVALID_HANDLE_VALUE ) && ( handle == INVALID_HANDLE_VALUE ) ) {
                handle = file_handle::invalid_value; // normalize null handle values (see note for win32 handle invalid_value)
            }
            return handle;
        }
    }; // struct create_file
} // namespace detail

BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 )
file_handle create_file( char    const * const file_name, flags::opening const flags ) noexcept { return file_handle{ detail::create_file::do_create( file_name, flags ) }; }
file_handle create_file( wchar_t const * const file_name, flags::opening const flags ) noexcept { return file_handle{ detail::create_file::do_create( file_name, flags ) }; }


bool delete_file( char    const * const file_name ) noexcept { return ::DeleteFileA( file_name ) != false; }
bool delete_file( wchar_t const * const file_name ) noexcept { return ::DeleteFileW( file_name ) != false; }

BOOST_NOINLINE
err::fallible_result<void, error> set_size( file_handle::reference const file_handle, std::uint64_t const desired_size ) noexcept
{
    // It is 'OK' to send null/invalid handles to Windows functions (they will
    // simply fail), this simplifies error handling (it is enough to go through
    // all the logic, inspect the final result and then 'throw' on error).
    BOOST_VERIFY
    (
        ::SetFilePointerEx( file_handle, reinterpret_cast<LARGE_INTEGER const &>( desired_size ), nullptr, FILE_BEGIN ) ||
        ( file_handle == handle_traits::invalid_value )
    );

    auto const success{ ::SetEndOfFile( file_handle ) };

    // TODO: rethink this - rewind?
    BOOST_VERIFY
    (
        ::SetFilePointerEx( file_handle, { .QuadPart = 0 }, nullptr, FILE_BEGIN ) ||
        ( file_handle == handle_traits::invalid_value )
    );
    if ( success ) [[ unlikely ]]
        return err::success;
    return error{};
}


std::uint64_t get_size( file_handle::reference const file_handle ) noexcept
{
    LARGE_INTEGER file_size;
    BOOST_VERIFY( ::GetFileSizeEx( file_handle, &file_size ) || ( file_handle == handle_traits::invalid_value ) );
    return file_size.QuadPart;
}


namespace detail
{
    std::uint64_t get_section_size( HANDLE const mapping_handle ) noexcept { return get_size( mapping::const_handle{ mapping_handle } ); }

    // https://support.microsoft.com/en-us/kb/125713 Common File Mapping Problems and Platform Differences
    namespace create_mapping_impl
    {
        BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE HANDLE call_create( char    const * __restrict const file_name, auto const ... args ) { return ::CreateFileMappingA( args..., file_name ); }
        BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE HANDLE call_create( wchar_t const * __restrict const file_name, auto const ... args ) { return ::CreateFileMappingW( args..., file_name ); }

        BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE HANDLE call_open  ( char    const * __restrict const file_name, auto const ... args ) { return ::OpenFileMappingA  ( args..., file_name ); }
        BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE HANDLE call_open  ( wchar_t const * __restrict const file_name, auto const ... args ) { return ::OpenFileMappingW  ( args..., file_name ); }

        // Windows (11 23H2) does not (still) seem to support resizing of pagefile-backed mappings
        // so we emulate those by creating a 2GB one and counting on:
        // - the NT kernel to be lazy and
        // - that amount to be 'enough for everyone'.
        extern std::size_t const max_anonymous_pf_mapping_size{ std::numeric_limits<std::int32_t>::max() };

        using desired_access_t = flags::mapping::access_rights::object;

        BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 )
        HANDLE map_file( file_handle::reference const file, desired_access_t const ap, flags::flags_t const page_protection, std::uint64_t const size ) noexcept
        {
            HANDLE handle{ handle_traits::invalid_value };
            LARGE_INTEGER maximum_size{ .QuadPart = static_cast<LONGLONG>( size ) };
            BOOST_ASSERT_MSG( std::uint64_t( maximum_size.QuadPart ) == size, "Unsupported section size" );
            if ( !file ) // ignoring size?
                maximum_size.QuadPart = max_anonymous_pf_mapping_size;
        
            auto const nt_result
            {
                nt::NtCreateSection // TODO use it for named sections also
                (
                    &handle,
                    SECTION_EXTEND_SIZE | SECTION_QUERY | STANDARD_RIGHTS_REQUIRED | ap.privileges,
                    nullptr, // OBJECT_ATTRIBUTES
                    &maximum_size,
                    page_protection,
                    SEC_RESERVE, // SEC_COMMIT can quickly hit into STATUS_COMMITMENT_LIMIT so we rely on committing on demand when mapping views
                    file
                )
            };
            BOOST_VERIFY( NT_SUCCESS( nt_result ) ); // TODO SetLastError until fully switching to NTAPI
            return handle;
        }
        HANDLE map_file( file_handle::reference const file, desired_access_t const ap, flags::flags_t const page_protection ) noexcept { return map_file( file, ap, page_protection, 0 ); }

        void clear( HANDLE & mapping_handle )
        {
            handle::traits::close( mapping_handle );
            mapping_handle = nullptr;
        }

        BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_FORCEINLINE
        mapping do_map( file_handle && file, flags::mapping const flags, std::uint64_t const maximum_size, auto const * __restrict const name ) noexcept
        {
            // CreateFileMapping wants this instead of null
            auto const fh{ ( file.get() == file_handle::invalid_value ) ? INVALID_HANDLE_VALUE : file.get() };

            ::SECURITY_ATTRIBUTES sa;
            auto const p_security_attributes( flags::detail::make_sa_ptr( sa, flags.ap.system_access.p_sd, reinterpret_cast<bool const &>/*static_cast<bool>*/( flags.ap.child_access ) ) );
            auto const max_sz               ( reinterpret_cast<ULARGE_INTEGER const &>( maximum_size ) );
            auto /*const*/ mapping_handle
            (
                call_create( name, fh, const_cast<::LPSECURITY_ATTRIBUTES>( p_security_attributes ), flags.page_protection, max_sz.HighPart, max_sz.LowPart )
            );
            BOOST_ASSERT_MSG
            (
                ( fh != INVALID_HANDLE_VALUE ) || maximum_size,
                "CreateFileMapping accepts INVALID_HANDLE_VALUE as valid input but only "
                "if the size parameter is not zero."
            );
            auto const creation_disposition{ flags.creation_disposition    };
            auto const error               { err::last_win32_error::get()  };
            auto const preexisting         { error == ERROR_ALREADY_EXISTS };
            using disposition = flags::named_object_construction_policy;
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

            return { mapping_handle, flags.map_view_flags(), flags.ap.object_access, std::move( file ) };
        }
    } // namepsace create_mapping_impl
} // namespace detail

mapping create_mapping( file_handle && file, flags::mapping const flags, std::uint64_t const maximum_size, char const * const name ) noexcept
{
    return detail::create_mapping_impl::do_map( std::move( file ), flags, maximum_size, name );
}

#if 0
mapping create_mapping( handle::reference const file, flags::mapping const flags ) noexcept
{
    auto const mapping_handle
    (
        detail::create_mapping_impl::map_file( file, flags.create_mapping_flags )
    );
    return { mapping_handle, flags.map_view_flags };
}
#endif

mapping create_mapping
(
    file_handle                             &&       file,
    flags::access_privileges::object           const object_access,
    [[ maybe_unused ]]
    flags::access_privileges::child_process    const child_access,
    flags::mapping          ::share_mode       const share_mode,
    std  ::size_t                              const size
) noexcept
{
    auto const page_protection{ flags::detail::object_access_to_page_access( object_access, share_mode ) };
    auto const mapping_handle
    (
        detail::create_mapping_impl::map_file( file, object_access, page_protection, size )
    );
    return { mapping_handle, { page_protection }, object_access, std::move( file ) };
}

//------------------------------------------------------------------------------
} // win32
//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
