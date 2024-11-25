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
#include <psi/vm/detail/win32.hpp>
#include <psi/vm/flags/flags.win32.hpp>

#include <accctrl.h>
#include <aclapi.h>

#include <memory>
#include <span>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace win32
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

static_assert( (flags_t)named_object_construction_policy::create_new                      == CREATE_NEW       , "" );
static_assert( (flags_t)named_object_construction_policy::create_new_or_truncate_existing == CREATE_ALWAYS    , "" );
static_assert( (flags_t)named_object_construction_policy::open_existing                   == OPEN_EXISTING    , "" );
static_assert( (flags_t)named_object_construction_policy::open_or_create                  == OPEN_ALWAYS      , "" );
static_assert( (flags_t)named_object_construction_policy::open_and_truncate_existing      == TRUNCATE_EXISTING, "" );

static_assert( access_privileges::read    == ( GENERIC_READ    | FILE_MAP_READ                          ), "" );
static_assert( access_privileges::write   == ( GENERIC_WRITE   | FILE_MAP_WRITE                         ), "" );
static_assert( access_privileges::execute == ( GENERIC_EXECUTE | FILE_MAP_EXECUTE                       ), "" );
static_assert( access_privileges::all     == ( GENERIC_ALL     | FILE_MAP_ALL_ACCESS | FILE_MAP_EXECUTE ), "" );

namespace detail
{
    [[ noreturn, gnu::cold ]]
    void throw_bad_alloc()
    {
        if constexpr ( requires{ std::_Xbad_alloc(); } )
            std::_Xbad_alloc();
        else
            throw std::bad_alloc{};
    }

    // https://msdn.microsoft.com/en-us/library/windows/desktop/aa446595(v=vs.85).aspx Creating a Security Descriptor for a New Object in C++
    // https://msdn.microsoft.com/en-us/library/windows/desktop/aa379602(v=vs.85).aspx SID strings
    // https://technet.microsoft.com/en-us/library/cc780850(v=ws.10).aspx Security identifiers
    // https://technet.microsoft.com/en-us/library/cc781716(v=ws.10).aspx How Security Descriptors and Access Control Lists Work
    // http://windowsitpro.com/networking/understanding-well-known-security-principals-part-1
    // http://www.codeproject.com/Articles/10200/The-Windows-Access-Control-Model-Part
    // https://www.osronline.com/article.cfm?article=56 Keeping Secrets - Windows NT Security (Part I)
    // http://blogs.technet.com/b/askds/archive/2009/06/01/null-and-empty-dacls.aspx
    // SECURITY_MAX_SID_SIZE
    BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1, BOOST_RESTRICTED_FUNCTION_RETURN )
    dynamic_sd const * make_sd( EXPLICIT_ACCESSW const * __restrict const p_ea_entries, std::uint8_t const number_of_entries ) noexcept //KEY_READ
    {
        ACL * p_acl;
        auto const result_code( ::SetEntriesInAclW( number_of_entries, const_cast<PEXPLICIT_ACCESS_W>( p_ea_entries ), nullptr, &p_acl ) );
        if ( result_code != ERROR_SUCCESS )
        {
            BOOST_ASSERT( result_code == ERROR_NOT_ENOUGH_MEMORY );
            throw_bad_alloc();
        }

        SECURITY_DESCRIPTOR sd; static_assert( sizeof( sd ) >= SECURITY_DESCRIPTOR_MIN_LENGTH );
        BOOST_VERIFY( ::InitializeSecurityDescriptor( &sd, SECURITY_DESCRIPTOR_REVISION ) );
        BOOST_VERIFY( ::SetSecurityDescriptorDacl   ( &sd, true, p_acl, false           ) );

#   if 0 // disabled - get/use all user's groups (as opposed to only the 'primary') approach
        HANDLE token;
        if ( !::OpenThreadToken( ::GetCurrentThread(), TOKEN_QUERY, false, &token ) )
        {}

        DWORD required_size;
        BOOST_VERIFY( ::GetTokenInformation( token, TokenGroups, nullptr, 0, &required_size ) );
        std::span const groups
        {
            static_cast<TOKEN_GROUPS *>( ::_alloca( required_size ) ), required_size / sizeof( TOKEN_GROUPS ) )
        };
        BOOST_VERIFY( ::GetTokenInformation( token, TokenGroups, groups.begin(), required_size, &required_size ) );
#   endif // disabled
        //RtlAbsoluteToSelfRelativeSD
        DWORD length( 0 );
        BOOST_VERIFY( ::MakeSelfRelativeSD( &sd, nullptr, &length ) == false && ::GetLastError() == ERROR_INSUFFICIENT_BUFFER );
        length += sizeof( dynamic_sd ) - sizeof( SECURITY_DESCRIPTOR );
        auto const p_dsd( reinterpret_cast<dynamic_sd *>( new (std::nothrow) char[ length ] ) );
        BOOST_VERIFY( ::MakeSelfRelativeSD( &sd, static_cast<SECURITY_DESCRIPTOR *>( p_dsd ), &length ) || !p_dsd );
        BOOST_VERIFY( ::LocalFree( p_acl ) == nullptr );
        if ( !p_dsd )
            throw_bad_alloc();
        p_dsd->reset  ();
        p_dsd->add_ref();
        return p_dsd;
    }

    dynamic_sd const * make_sd( std::uint32_t const permissions, wchar_t const * __restrict const trustee ) noexcept
    {
        EXPLICIT_ACCESSW ea;
        ea.grfAccessPermissions = permissions;
        ea.grfAccessMode        = SET_ACCESS;
        ea.grfInheritance       = NO_INHERITANCE;
        ea.Trustee.TrusteeForm  = TRUSTEE_IS_NAME; // TRUSTEE_IS_SID
        ea.Trustee.TrusteeType  = TRUSTEE_IS_WELL_KNOWN_GROUP;
        ea.Trustee.ptstrName    = const_cast<LPWSTR>( trustee );
        return make_sd( &ea, 1 );
    }

    BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_RETURN )
    dynamic_sd const * BOOST_CC_REG make_sd( scope_privileges const permissions )
    {
        EXPLICIT_ACCESSW entries[ permissions.size() ];
        std::uint8_t     count( 0 );

        // http://windowsitpro.com/networking/understanding-well-known-security-principals-part-1
        static /*std::array<*/wchar_t const * __restrict const/*, 3> const*/ trustee_names[] =
        {
            //L"S-1-5-10", // Principal Self
            L"CREATOR OWNER",//L"S-1-3-0" , // Creator Owner ID
            L"CREATOR GROUP",//L"S-1-3-1" , // SID_CREATOR_GROUP
            L"Everyone"     ,//L"S-1-1-0" , // EVERYONE SID_WORLD
        };

        for ( std::uint8_t scope( 0 ); scope < permissions.size(); ++scope )
        {
            auto const scope_flags( permissions[ scope ] );
            if ( !scope_flags )
                continue;
            EXPLICIT_ACCESSW & ea( entries[ count++ ] );
            ea.grfAccessPermissions             = scope_flags;
            ea.grfAccessMode                    = SET_ACCESS;
            ea.grfInheritance                   = NO_INHERITANCE;
            ea.Trustee.pMultipleTrustee         = nullptr;
            ea.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
            ea.Trustee.TrusteeForm              = TRUSTEE_IS_NAME;
            ea.Trustee.TrusteeType              = TRUSTEE_IS_UNKNOWN; // TRUSTEE_IS_WELL_KNOWN_GROUP;
            ea.Trustee.ptstrName                = const_cast<LPWSTR>( trustee_names[ scope ] );
        }

        return make_sd( entries, count );
    }
} // namespace detail

namespace
{
#ifdef NDEBUG
    constexpr
#endif // NDEBUG
    SECURITY_DESCRIPTOR BOOST_CC_REG make_all_shall_pass()
    {
        SECURITY_DESCRIPTOR constexpr sd = { SECURITY_DESCRIPTOR_REVISION, 0, SE_DACL_PRESENT, nullptr, nullptr, nullptr, nullptr };
        static_assert( sizeof( sd ) >= SECURITY_DESCRIPTOR_MIN_LENGTH, "Windows API assumption broken" );
    #ifndef NDEBUG
        SECURITY_DESCRIPTOR sd_with_cleared_padding; std::memset( &sd_with_cleared_padding, 0, sizeof( sd_with_cleared_padding ) );
        sd_with_cleared_padding.Revision = sd.Revision;
        sd_with_cleared_padding.Sbz1     = sd.Sbz1    ;
        sd_with_cleared_padding.Control  = sd.Control ;
        sd_with_cleared_padding.Owner    = sd.Owner   ;
        sd_with_cleared_padding.Group    = sd.Group   ;
        sd_with_cleared_padding.Sacl     = sd.Sacl    ;
        sd_with_cleared_padding.Dacl     = sd.Dacl    ;
        SECURITY_DESCRIPTOR debugger;
        BOOST_VERIFY    ( ::InitializeSecurityDescriptor( &debugger, SECURITY_DESCRIPTOR_REVISION ) );
        BOOST_VERIFY    ( ::SetSecurityDescriptorDacl   ( &debugger, true, nullptr, false         ) );
        BOOST_ASSERT_MSG( std::memcmp( &sd_with_cleared_padding, &debugger, sizeof( sd ) ) == 0, "Windows API assumption broken" );
    #endif // NDEBUG
        return sd;
    }

    SECURITY_DESCRIPTOR const all_shall_pass( make_all_shall_pass() );
} // anonymous namespace

access_privileges::system const access_privileges::system::process_default = { nullptr        , false };
access_privileges::system const access_privileges::system::unrestricted    = { &all_shall_pass, false };
access_privileges::system const access_privileges::system::nix_default     = { detail::make_sd
                                                                               (
                                                                                   access_privileges::system::user ( access_privileges::all  ) |
                                                                                   access_privileges::system::group( access_privileges::read ) |
                                                                                   access_privileges::system::world( access_privileges::read )
                                                                               ),
                                                                               true
                                                                             };
access_privileges::system const access_privileges::system::_644            = { ( nix_default.get_dynamic_sd().add_ref(), nix_default.p_sd ), true }; //access_privileges::system::nix_default;

//------------------------------------------------------------------------------
} // flags
//------------------------------------------------------------------------------
} // win32
//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
