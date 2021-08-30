////////////////////////////////////////////////////////////////////////////////
///
/// \file detail/nt.hpp
/// -------------------
///
/// Copyright (c) Domagoj Saric 2015 - 2021.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
/// (See accompanying file LICENSE_1_0.txt or copy at
/// http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
/// http://undocumented.ntinternals.net
/// http://ultradefrag.sourceforge.net/doc/man/Windows%20NT(2000)%20Native%20API%20Reference.pdf
/// https://webdiis.unizar.es/~spd/pub/windows/ntdll.htm
/// https://technet.microsoft.com/en-us/sysinternals/bb896657.aspx WinObj
/// https://www.osronline.com/article.cfm?id=91
/// http://downloads.securityfocus.com/vulnerabilities/exploits/20940-Ivan.c
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef nt_hpp__532C3d08_E487_4548_B51D_1E64CD74dE9B
#define nt_hpp__532C3d08_E487_4548_B51D_1E64CD74dE9B
#pragma once
//------------------------------------------------------------------------------
#include <boost/assert.hpp>
#include <boost/config.hpp>

#ifndef _WIN32_WINNT
#   define _WIN32_WINNT 0x0700
#endif // _WIN32_WINNT
#include <winternl.h>

#pragma comment( lib, "ntdll.lib" )
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
namespace nt
{
//------------------------------------------------------------------------------

namespace detail
{
    inline HMODULE const ntdll( ::GetModuleHandleW( L"ntdll.dll" ) );
    
    inline BOOST_ATTRIBUTES( BOOST_COLD, BOOST_RESTRICTED_FUNCTION_L3, BOOST_RESTRICTED_FUNCTION_RETURN )
    void * BOOST_CC_REG get_nt_proc( char const * const proc_name )
    {
        BOOST_ASSERT( ntdll );
        auto const result( ::GetProcAddress( ntdll, proc_name ) );
        BOOST_ASSERT( result );
        return result;
    }

    template <typename Proc>
    Proc * get_nt_proc( char const * const proc_name )
    {
        return reinterpret_cast<Proc *>( get_nt_proc( proc_name ) );
    }

    typedef
    NTSTATUS (WINAPI BaseGetNamedObjectDirectory_t)( HANDLE * phDir );

    typedef
    NTSYSAPI NTSTATUS (NTAPI NtCreateSection_t)
    (
        OUT PHANDLE            SectionHandle,
        IN ULONG               DesiredAccess,
        IN POBJECT_ATTRIBUTES  ObjectAttributes OPTIONAL,
        IN PLARGE_INTEGER      MaximumSize OPTIONAL,
        IN ULONG               PageAttributess,
        IN ULONG               SectionAttributes,
        IN HANDLE              FileHandle OPTIONAL
    );

    enum SECTION_INFORMATION_CLASS { SectionBasicInformation, SectionImageInformation };

    struct SECTION_BASIC_INFORMATION
    {
        PVOID         BaseAddress;
        ULONG         SectionAttributes;
        LARGE_INTEGER SectionSize;
    };

    typedef
    NTSYSAPI NTSTATUS (NTAPI NtQuerySection_t)
    (
        IN  HANDLE                    SectionHandle,
        IN  SECTION_INFORMATION_CLASS InformationClass,
        OUT PVOID                     InformationBuffer,
        IN  ULONG                     InformationBufferSize,
        OUT PULONG                    ResultLength OPTIONAL
    );

    __declspec( selectany ) extern auto const NtQuerySection( get_nt_proc<NtQuerySection_t>( "NtQuerySection" ) );

    typedef
    NTSYSAPI NTSTATUS (NTAPI NtExtendSection_t)
    (
        IN HANDLE         SectionHandle,
        IN PLARGE_INTEGER NewSectionSize
    );
} // namespace detail

//------------------------------------------------------------------------------
} // nt
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // nt_hpp
