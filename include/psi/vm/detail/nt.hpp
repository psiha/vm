////////////////////////////////////////////////////////////////////////////////
///
/// \file detail/nt.hpp
/// -------------------
///
/// Copyright (c) Domagoj Saric 2015 - 2026.
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

#include <psi/build/disable_warnings.hpp>

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

// TODO move to https://github.com/winsiderss/phnt
// https://www.jeremyong.com/winapi/io/2024/11/03/windows-memory-mapped-file-io
// https://devblogs.microsoft.com/oldnewthing/20160701-00/?p=93785 How exactly are page tables allocated on demand for large reserved regions?

#ifndef STATUS_SUCCESS
struct NTSTATUS_BITS
{
    std::uint32_t code     : 16;
    std::uint32_t facility : 12;
    std::uint32_t reserved : 1;
    std::uint32_t customer : 1;

    // 0x0: Success
    // 0x1: Informational
    // 0x2: Warning
    // 0x3: Error
    std::uint32_t severity : 2;
};
using NTSTATUS = LONG;
static_assert( sizeof( NTSTATUS_BITS ) == sizeof( NTSTATUS ) );
NTSTATUS constexpr STATUS_SUCCESS{};
#endif // STATUS_SUCCESS
#ifndef STATUS_CONFLICTING_ADDRESSES
auto constexpr STATUS_CONFLICTING_ADDRESSES{ NTSTATUS( 0xC0000018 ) };
#endif
#ifndef STATUS_SECTION_NOT_EXTENDED
auto constexpr STATUS_SECTION_NOT_EXTENDED{ NTSTATUS( 0xC0000087 ) };
#endif

inline auto const current_process{ reinterpret_cast<HANDLE>( std::intptr_t{ -1 } ) };

namespace detail
{
    BOOST_ATTRIBUTES( BOOST_COLD, BOOST_RESTRICTED_FUNCTION_L3, BOOST_RESTRICTED_FUNCTION_RETURN )
    ::PROC get_nt_proc( char const * proc_name ) noexcept;

    // Like get_nt_proc but returns nullptr if the function is not found (for
    // optional APIs that may not exist on older Windows versions).
    BOOST_ATTRIBUTES( BOOST_COLD )
    ::PROC try_get_nt_proc( char const * proc_name ) noexcept;

    PSI_WARNING_DISABLE_PUSH()
    PSI_WARNING_CLANG_DISABLE( -Wcast-function-type-mismatch )
    template <typename ProcPtr>
    ProcPtr get_nt_proc( char const * const proc_name )
    {
        return reinterpret_cast<ProcPtr>( detail::get_nt_proc( proc_name ) );
    }
    template <typename ProcPtr>
    ProcPtr try_get_nt_proc( char const * const proc_name )
    {
        return reinterpret_cast<ProcPtr>( detail::try_get_nt_proc( proc_name ) );
    }
    PSI_WARNING_DISABLE_POP()
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
) noexcept;
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
) noexcept;
inline auto const NtQuerySection{ detail::get_nt_proc<NtQuerySection_t>( "NtQuerySection" ) };

using NtExtendSection_t = NTSTATUS (NTAPI*)
(
    IN HANDLE         SectionHandle,
    IN PLARGE_INTEGER NewSectionSize
) noexcept;
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
) noexcept;
inline auto const NtMapViewOfSection{ detail::get_nt_proc<NtMapViewOfSection_t>( "NtMapViewOfSection" ) };

enum MEMORY_INFORMATION_CLASS
{
    MemoryBasicInformation, // q: MEMORY_BASIC_INFORMATION
    MemoryWorkingSetInformation, // q: MEMORY_WORKING_SET_INFORMATION
    MemoryMappedFilenameInformation, // q: UNICODE_STRING
    MemoryRegionInformation, // q: MEMORY_REGION_INFORMATION
    MemoryWorkingSetExInformation, // q: MEMORY_WORKING_SET_EX_INFORMATION // since VISTA
    MemorySharedCommitInformation, // q: MEMORY_SHARED_COMMIT_INFORMATION // since WIN8
    MemoryImageInformation, // q: MEMORY_IMAGE_INFORMATION
    MemoryRegionInformationEx, // MEMORY_REGION_INFORMATION
    MemoryPrivilegedBasicInformation, // MEMORY_BASIC_INFORMATION
    MemoryEnclaveImageInformation, // MEMORY_ENCLAVE_IMAGE_INFORMATION // since REDSTONE3
    MemoryBasicInformationCapped, // 10
    MemoryPhysicalContiguityInformation, // MEMORY_PHYSICAL_CONTIGUITY_INFORMATION // since 20H1
    MemoryBadInformation, // since WIN11
    MemoryBadInformationAllProcesses, // since 22H1
    MemoryImageExtensionInformation, // MEMORY_IMAGE_EXTENSION_INFORMATION // since 24H2
    MaxMemoryInfoClass
};

using NtQueryVirtualMemory_t = NTSTATUS (NTAPI *)
(
    HANDLE                   ProcessHandle,
    PVOID                    BaseAddress,
    MEMORY_INFORMATION_CLASS MemoryInformationClass,
    PVOID                    MemoryInformation,
    SIZE_T                   MemoryInformationLength,
    PSIZE_T                  ReturnLength
) noexcept;
inline auto const NtQueryVirtualMemory{ detail::get_nt_proc<NtQueryVirtualMemory_t>( "NtQueryVirtualMemory" ) };


using NtAllocateVirtualMemory_t = NTSTATUS (NTAPI*)( IN HANDLE ProcessHandle, IN OUT PVOID * BaseAddress, ULONG_PTR ZeroBits, PSIZE_T RegionSize, ULONG allocation_type, ULONG Protect ) noexcept;
using NtFreeVirtualMemory_t     = NTSTATUS (NTAPI*)( IN HANDLE ProcessHandle, IN     PVOID * BaseAddress, PSIZE_T RegionSize, ULONG FreeType ) noexcept;
using NtProtectVirtualMemory_t  = NTSTATUS (NTAPI*)( IN HANDLE ProcessHandle, IN OUT PVOID * BaseAddress, IN OUT PULONG NumberOfBytesToProtect, IN ULONG NewAccessProtection, OUT PULONG OldAccessProtection ) noexcept;

inline auto const NtAllocateVirtualMemory{ detail::get_nt_proc<NtAllocateVirtualMemory_t>( "NtAllocateVirtualMemory" ) };
inline auto const NtFreeVirtualMemory    { detail::get_nt_proc<NtFreeVirtualMemory_t    >( "NtFreeVirtualMemory"     ) };

////////////////////////////////////////////////////////////////////////////////
// Windows 10 RS5 (1809+) placeholder virtual memory APIs
// https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc2
// https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile3
////////////////////////////////////////////////////////////////////////////////

#ifndef MEM_RESERVE_PLACEHOLDER
auto constexpr MEM_RESERVE_PLACEHOLDER  { ULONG( 0x00040000 ) };
#endif
#ifndef MEM_REPLACE_PLACEHOLDER
auto constexpr MEM_REPLACE_PLACEHOLDER  { ULONG( 0x00004000 ) };
#endif
#ifndef MEM_PRESERVE_PLACEHOLDER
auto constexpr MEM_PRESERVE_PLACEHOLDER { ULONG( 0x00000002 ) };
#endif
#ifndef MEM_COALESCE_PLACEHOLDERS
auto constexpr MEM_COALESCE_PLACEHOLDERS{ ULONG( 0x00000001 ) };
#endif

// NtAllocateVirtualMemoryEx - extended version supporting MEM_RESERVE_PLACEHOLDER
using NtAllocateVirtualMemoryEx_t = NTSTATUS (NTAPI*)
(
    IN     HANDLE                  ProcessHandle,
    IN OUT PVOID                  *BaseAddress,
    IN OUT PSIZE_T                 RegionSize,
    IN     ULONG                   AllocationType,
    IN     ULONG                   PageProtection,
    IN OUT PMEM_EXTENDED_PARAMETER ExtendedParameters OPTIONAL,
    IN     ULONG                   ExtendedParameterCount
) noexcept;

// NtMapViewOfSectionEx - extended version supporting MEM_REPLACE_PLACEHOLDER
using NtMapViewOfSectionEx_t = NTSTATUS (NTAPI*)
(
    IN     HANDLE                  SectionHandle,
    IN     HANDLE                  ProcessHandle,
    IN OUT PVOID                  *BaseAddress,
    IN OUT PLARGE_INTEGER          SectionOffset OPTIONAL,
    IN OUT PSIZE_T                 ViewSize,
    IN     ULONG                   AllocationType,
    IN     ULONG                   PageProtection,
    IN OUT PMEM_EXTENDED_PARAMETER ExtendedParameters OPTIONAL,
    IN     ULONG                   ExtendedParameterCount
) noexcept;

// NtUnmapViewOfSectionEx - extended version supporting MEM_PRESERVE_PLACEHOLDER
using NtUnmapViewOfSectionEx_t = NTSTATUS (NTAPI*)
(
    IN HANDLE ProcessHandle,
    IN PVOID  BaseAddress,
    IN ULONG  Flags
) noexcept;

// Placeholder VM APIs — unconditionally available (Win11+ minimum).
inline auto const NtAllocateVirtualMemoryEx{ detail::get_nt_proc<NtAllocateVirtualMemoryEx_t>( "NtAllocateVirtualMemoryEx" ) };
inline auto const NtMapViewOfSectionEx     { detail::get_nt_proc<NtMapViewOfSectionEx_t     >( "NtMapViewOfSectionEx"      ) };
inline auto const NtUnmapViewOfSectionEx   { detail::get_nt_proc<NtUnmapViewOfSectionEx_t   >( "NtUnmapViewOfSectionEx"    ) };

//------------------------------------------------------------------------------
} // psi::vm::nt
//------------------------------------------------------------------------------
