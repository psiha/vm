////////////////////////////////////////////////////////////////////////////////
///
/// \file expand_win32.hpp
/// ----------------------
///
/// Shared Windows placeholder-based expansion primitives.
/// Used by both remap.cpp::expand() and mapped_view::expand() to avoid
/// duplicating the NtAllocateVirtualMemoryEx / MEM_PRESERVE_PLACEHOLDER logic.
///
/// Copyright (c) Domagoj Saric 2023 - 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once
#ifdef _WIN32

#include <psi/vm/align.hpp>
#include <psi/vm/detail/nt.hpp>
#include <psi/vm/allocation.hpp>

#include <boost/assert.hpp>

#include <cstddef>
//------------------------------------------------------------------------------
namespace psi::vm::detail
{
//------------------------------------------------------------------------------

/// Try to expand in-place by splitting a trailing placeholder.
/// Returns true if the expansion succeeded (address range [addr, addr+current+additional) is now committed).
/// Returns false if no suitable placeholder exists or the operation failed.
[[ nodiscard, gnu::cold ]]
inline bool try_placeholder_expand(
    std::byte * const address,
    std::size_t const current_size,
    std::size_t const additional_size,
    allocation_type const alloc_type = allocation_type::commit
) noexcept
{
    MEMORY_BASIC_INFORMATION mbi;
    if
    (
        nt::NtQueryVirtualMemory
        (
            nt::current_process,
            address + current_size,
            nt::MemoryBasicInformation,
            &mbi, sizeof( mbi ), nullptr
        ) != nt::STATUS_SUCCESS
        || mbi.State      != MEM_RESERVE
        || mbi.RegionSize <  additional_size
    )
    {
        return false;
    }

    auto const placeholder_size{ static_cast<std::size_t>( mbi.RegionSize ) };
    auto * const adj{ address + current_size };
    bool can_replace{ true };

    // Split the placeholder if larger than needed.
    if ( placeholder_size > additional_size )
    {
        PVOID  split_addr{ adj };
        SIZE_T split_sz  { additional_size };
        can_replace = (
            nt::NtFreeVirtualMemory
            (
                nt::current_process,
                &split_addr,
                &split_sz,
                MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER
            ) == nt::STATUS_SUCCESS
        );
    }

    if ( !can_replace )
        return false;

    // Replace the (now exactly-sized) placeholder with committed memory.
    PVOID  replace_addr{ adj };
    SIZE_T replace_sz  { additional_size };
    return nt::NtAllocateVirtualMemoryEx
    (
        nt::current_process,
        &replace_addr,
        &replace_sz,
        std::to_underlying( alloc_type ) | std::to_underlying( allocation_type::reserve ) | MEM_REPLACE_PLACEHOLDER,
        PAGE_READWRITE,
        nullptr, 0
    ) == nt::STATUS_SUCCESS;
}


/// Try to expand a section view in-place by mapping additional section data
/// into a trailing placeholder.  Uses NtMapViewOfSectionEx(MEM_REPLACE_PLACEHOLDER).
/// Returns true on success (the section view now covers [address, address+target_size)).
[[ nodiscard, gnu::cold ]]
inline bool try_placeholder_section_expand(
    HANDLE        const section_handle,
    std::byte   * const address,
    std::size_t   const current_size,
    std::size_t   const additional_size,
    ULONG         const page_protection
) noexcept
{
    MEMORY_BASIC_INFORMATION mbi;
    if
    (
        nt::NtQueryVirtualMemory
        (
            nt::current_process,
            address + current_size,
            nt::MemoryBasicInformation,
            &mbi, sizeof( mbi ), nullptr
        ) != nt::STATUS_SUCCESS
        || mbi.State      != MEM_RESERVE
        || mbi.RegionSize <  additional_size
    )
    {
        return false;
    }

    auto const placeholder_size{ static_cast<std::size_t>( mbi.RegionSize ) };
    auto * const adj{ address + current_size };

    // Split the placeholder if larger than needed.
    if ( placeholder_size > additional_size )
    {
        PVOID  split_addr{ adj };
        SIZE_T split_sz  { additional_size };
        if
        (
            nt::NtFreeVirtualMemory
            (
                nt::current_process,
                &split_addr,
                &split_sz,
                MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER
            ) != nt::STATUS_SUCCESS
        )
        {
            return false;
        }
    }

    // Map the section view into the placeholder via NtMapViewOfSectionEx.
    PVOID           view_base{ adj };
    LARGE_INTEGER   offset;
    offset.QuadPart = static_cast<LONGLONG>( current_size );
    SIZE_T          view_size{ additional_size };

    auto const status
    {
        nt::NtMapViewOfSectionEx
        (
            section_handle,
            nt::current_process,
            &view_base,
            &offset,
            &view_size,
            MEM_REPLACE_PLACEHOLDER,
            page_protection,
            nullptr, 0
        )
    };
    return status == nt::STATUS_SUCCESS;
}


/// Allocate a new section view with 2x over-reservation (trailing placeholder).
/// Maps section data from [section_offset, section_offset + aligned_size) into
/// the first half; the second half remains as a placeholder for future
/// in-place expansion via try_placeholder_section_expand().
/// Returns the base address on success (caller must handle COW data transfer),
/// or nullptr on failure.
/// On success: [section view aligned_size | placeholder headroom].
[[ nodiscard, gnu::cold ]]
inline std::byte * overreserve_section_map(
    HANDLE          const section_handle,
    std::size_t     const desired_size,
    std::uint64_t   const section_offset,
    ULONG           const page_protection
) noexcept
{
    auto const aligned_size { align_up( desired_size, reserve_granularity ) };
    auto const headroom_size{ align_up( aligned_size * std::size_t{ 2 }, reserve_granularity ) };

    // Step 1: reserve a full placeholder region.
    PVOID  ph_addr{ nullptr };
    SIZE_T ph_size{ headroom_size };
    if
    (
        nt::NtAllocateVirtualMemoryEx
        (
            nt::current_process,
            &ph_addr,
            &ph_size,
            std::to_underlying( allocation_type::reserve ) | MEM_RESERVE_PLACEHOLDER,
            PAGE_NOACCESS,
            nullptr, 0
        ) != nt::STATUS_SUCCESS
    )
    {
        return nullptr;
    }

    auto * const base{ static_cast<std::byte *>( ph_addr ) };

    // Step 2: split [aligned_size placeholder | headroom placeholder].
    PVOID  split_addr{ ph_addr };
    SIZE_T split_sz  { aligned_size };
    if
    (
        nt::NtFreeVirtualMemory
        (
            nt::current_process,
            &split_addr,
            &split_sz,
            MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER
        ) != nt::STATUS_SUCCESS
    )
    {
        PVOID  free_addr{ ph_addr };
        SIZE_T free_sz  { 0 };
        nt::NtFreeVirtualMemory( nt::current_process, &free_addr, &free_sz, MEM_RELEASE );
        return nullptr;
    }

    // Step 3: map section view into the first placeholder.
    PVOID         view_base{ base };
    LARGE_INTEGER nt_offset;
    nt_offset.QuadPart = static_cast<LONGLONG>( section_offset );
    SIZE_T        view_size{ aligned_size };
    if
    (
        nt::NtMapViewOfSectionEx
        (
            section_handle,
            nt::current_process,
            &view_base,
            &nt_offset,
            &view_size,
            MEM_REPLACE_PLACEHOLDER,
            page_protection,
            nullptr, 0
        ) != nt::STATUS_SUCCESS
    )
    {
        // Map failed — free both placeholders.
        PVOID  free1{ base };
        SIZE_T sz1  { 0 };
        nt::NtFreeVirtualMemory( nt::current_process, &free1, &sz1, MEM_RELEASE );

        auto * const tail{ base + aligned_size };
        PVOID  free2{ tail };
        SIZE_T sz2  { 0 };
        nt::NtFreeVirtualMemory( nt::current_process, &free2, &sz2, MEM_RELEASE );
        return nullptr;
    }

    // Trailing placeholder at base + aligned_size remains for future expansion.
    return base;
}


/// Allocate a new region with 2x over-reservation (trailing placeholder).
/// Returns the new base address on success (caller must memcpy used data),
/// or nullptr on failure.
/// On success, the region layout is: [committed target_size | placeholder headroom].
[[ nodiscard, gnu::cold ]]
inline std::byte * overreserve_expand(
    std::size_t const target_size,
    allocation_type const alloc_type = allocation_type::commit
) noexcept
{
    auto const headroom_size{ align_up( target_size * std::size_t{ 2 }, reserve_granularity ) };
    PVOID  ph_addr{ nullptr };
    SIZE_T ph_size{ headroom_size };

    if
    (
        nt::NtAllocateVirtualMemoryEx
        (
            nt::current_process,
            &ph_addr,
            &ph_size,
            std::to_underlying( allocation_type::reserve ) | MEM_RESERVE_PLACEHOLDER,
            PAGE_NOACCESS,
            nullptr, 0
        ) != nt::STATUS_SUCCESS
    )
    {
        return nullptr;
    }

    auto * const base{ static_cast<std::byte *>( ph_addr ) };

    // Split: [target_size placeholder | headroom placeholder]
    PVOID  split_addr{ ph_addr };
    SIZE_T split_sz  { target_size };
    if
    (
        nt::NtFreeVirtualMemory
        (
            nt::current_process,
            &split_addr,
            &split_sz,
            MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER
        ) != nt::STATUS_SUCCESS
    )
    {
        // Split failed — free the entire placeholder
        PVOID  free_addr{ ph_addr };
        SIZE_T free_sz  { 0 };
        nt::NtFreeVirtualMemory( nt::current_process, &free_addr, &free_sz, MEM_RELEASE );
        return nullptr;
    }

    // Replace the first placeholder with committed memory
    PVOID  commit_addr{ base };
    SIZE_T commit_sz  { target_size };
    if
    (
        nt::NtAllocateVirtualMemoryEx
        (
            nt::current_process,
            &commit_addr,
            &commit_sz,
            std::to_underlying( alloc_type ) | std::to_underlying( allocation_type::reserve ) | MEM_REPLACE_PLACEHOLDER,
            PAGE_READWRITE,
            nullptr, 0
        ) != nt::STATUS_SUCCESS
    )
    {
        // Replace failed — free both placeholders
        PVOID  free1{ base };
        SIZE_T sz1  { 0 };
        nt::NtFreeVirtualMemory( nt::current_process, &free1, &sz1, MEM_RELEASE );

        auto * tail{ base + target_size };
        PVOID  free2{ tail };
        SIZE_T sz2  { 0 };
        nt::NtFreeVirtualMemory( nt::current_process, &free2, &sz2, MEM_RELEASE );
        return nullptr;
    }

    // Success: trailing headroom placeholder at base+target_size remains
    // for future in-place expansion via try_placeholder_expand.
    return base;
}

//------------------------------------------------------------------------------
} // namespace psi::vm::detail
//------------------------------------------------------------------------------
#endif // _WIN32
