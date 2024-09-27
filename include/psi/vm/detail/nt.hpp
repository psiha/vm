////////////////////////////////////////////////////////////////////////////////
///
/// \file detail/nt.hpp
/// -------------------
///
/// Copyright (c) Domagoj Saric 2015 - 2024.
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
#pragma once

#include <boost/config_ex.hpp>

#ifndef _WIN32_WINNT
#   define _WIN32_WINNT 0x0A00 // _WIN32_WINNT_WIN10
#endif // _WIN32_WINNT
#include <windows.h>
#include <winternl.h>

#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm::nt
{
//------------------------------------------------------------------------------

#ifndef STATUS_SUCCESS
using NTSTATUS = LONG;
NTSTATUS constexpr STATUS_SUCCESS{ 0 };
#endif // STATUS_SUCCESS
#ifndef STATUS_CONFLICTING_ADDRESSES
auto constexpr STATUS_CONFLICTING_ADDRESSES{ NTSTATUS( 0xC0000018 ) };
#endif

inline auto const current_process{ reinterpret_cast<HANDLE>( std::intptr_t{ -1 } ) };

namespace detail
{
    BOOST_ATTRIBUTES( BOOST_COLD, BOOST_RESTRICTED_FUNCTION_L3, BOOST_RESTRICTED_FUNCTION_RETURN )
    ::PROC get_nt_proc( char const * proc_name ) noexcept;

    template <typename ProcPtr>
    ProcPtr get_nt_proc( char const * const proc_name )
    {
        return reinterpret_cast<ProcPtr>( detail::get_nt_proc( proc_name ) );
    }
} // namespace detail

using BaseGetNamedObjectDirectory_t = NTSTATUS (WINAPI*)( HANDLE * phDir );

using NtCreateSection_t = NTSTATUS (NTAPI*)
(
    OUT PHANDLE            SectionHandle,
    IN ULONG               DesiredAccess,
    IN POBJECT_ATTRIBUTES  ObjectAttributes OPTIONAL,
    IN PLARGE_INTEGER      MaximumSize OPTIONAL,
    IN ULONG               PageAttributess,
    IN ULONG               SectionAttributes,
    IN HANDLE              FileHandle OPTIONAL
);
inline auto const NtCreateSection{ detail::get_nt_proc<NtCreateSection_t>( "NtCreateSection" ) };

enum SECTION_INFORMATION_CLASS { SectionBasicInformation, SectionImageInformation };

struct SECTION_BASIC_INFORMATION
{
    PVOID         BaseAddress;
    ULONG         SectionAttributes;
    LARGE_INTEGER SectionSize;
};

using NtQuerySection_t = NTSTATUS (NTAPI*)
(
    IN  HANDLE                    SectionHandle,
    IN  SECTION_INFORMATION_CLASS InformationClass,
    OUT PVOID                     InformationBuffer,
    IN  ULONG                     InformationBufferSize,
    OUT PULONG                    ResultLength OPTIONAL
);
inline auto const NtQuerySection{ detail::get_nt_proc<NtQuerySection_t>( "NtQuerySection" ) };

using NtExtendSection_t = NTSTATUS (NTAPI*)
(
    IN HANDLE         SectionHandle,
    IN PLARGE_INTEGER NewSectionSize
);
inline auto const NtExtendSection{ detail::get_nt_proc<NtExtendSection_t>( "NtExtendSection" ) };

enum SECTION_INHERIT
{
    ViewShare = 1,
    ViewUnmap = 2
};
using NtMapViewOfSection_t = NTSTATUS (NTAPI*)
(
    IN HANDLE             SectionHandle,
    IN HANDLE             ProcessHandle,
    IN OUT PVOID          *BaseAddress OPTIONAL,
    IN ULONG_PTR          ZeroBits OPTIONAL,
    IN SIZE_T             CommitSize,
    IN OUT PLARGE_INTEGER SectionOffset OPTIONAL,
    IN OUT PSIZE_T        ViewSize,
    IN SECTION_INHERIT    InheritDisposition,
    IN ULONG              AllocationType OPTIONAL,
    IN ULONG              Protect
);
inline auto const NtMapViewOfSection{ detail::get_nt_proc<NtMapViewOfSection_t>( "NtMapViewOfSection" ) };

using NtAllocateVirtualMemory_t = NTSTATUS (NTAPI*)( IN HANDLE ProcessHandle, IN OUT PVOID * BaseAddress, ULONG_PTR ZeroBits, PSIZE_T RegionSize, ULONG allocation_type, ULONG Protect );
using NtFreeVirtualMemory_t     = NTSTATUS (NTAPI*)( IN HANDLE ProcessHandle, IN     PVOID * BaseAddress, PSIZE_T RegionSize, ULONG FreeType );
using NtProtectVirtualMemory_t  = NTSTATUS (NTAPI*)( IN HANDLE ProcessHandle, IN OUT PVOID * BaseAddress, IN OUT PULONG NumberOfBytesToProtect, IN ULONG NewAccessProtection, OUT PULONG OldAccessProtection );

inline auto const NtAllocateVirtualMemory{ detail::get_nt_proc<NtAllocateVirtualMemory_t>( "NtAllocateVirtualMemory" ) };
inline auto const NtFreeVirtualMemory    { detail::get_nt_proc<NtFreeVirtualMemory_t    >( "NtFreeVirtualMemory"     ) };

//------------------------------------------------------------------------------
} // psi::vm::nt
//------------------------------------------------------------------------------
