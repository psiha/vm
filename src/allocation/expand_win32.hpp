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

// --- Low-level placeholder helpers ------------------------------------------

[[ nodiscard ]]
inline MEMORY_BASIC_INFORMATION query_vm( void const * const address ) noexcept
{
    MEMORY_BASIC_INFORMATION mbi;
    BOOST_VERIFY( nt::NtQueryVirtualMemory
    (
        nt::current_process,
        const_cast<void *>( address ),
        nt::MemoryBasicInformation,
        &mbi, sizeof( mbi ), nullptr
    ) == nt::STATUS_SUCCESS );
    return mbi;
}

/// Query the MEM_RESERVE placeholder (if any) at the given address.
/// Returns the placeholder size, or 0 if no suitable placeholder exists.
[[ nodiscard ]]
inline std::size_t query_placeholder( void const * const address ) noexcept
{
    auto const mbi{ query_vm( address ) };
    if ( mbi.State == MEM_RESERVE )
    {
        return mbi.RegionSize;
    }
    return 0;
}

/// Query whether the address is a standalone MEM_RESERVE placeholder
/// (AllocationBase == address). Used to detect trailing placeholders from
/// overreserve operations without accidentally freeing unrelated reservations.
[[ nodiscard ]]
inline bool is_standalone_placeholder( void const * const address ) noexcept
{
    auto const mbi{ query_vm( address ) };
    return ( mbi.State == MEM_RESERVE ) && ( mbi.AllocationBase == address );
}

/// Free an entire placeholder at the given address (size=0 → release whole allocation).
inline void free_placeholder( void * const address ) noexcept
{
    PVOID  addr{ address };
    SIZE_T sz  { 0 };
    BOOST_VERIFY( nt::NtFreeVirtualMemory( nt::current_process, &addr, &sz, MEM_RELEASE ) == nt::STATUS_SUCCESS );
}

/// Split a placeholder at [address, address+split_size) | [address+split_size, ...).
/// Returns true on success.
[[ nodiscard ]]
inline bool split_placeholder( void * const address, std::size_t const split_size ) noexcept
{
    PVOID  addr{ address };
    SIZE_T sz  { split_size };
    return nt::NtFreeVirtualMemory
    (
        nt::current_process,
        &addr,
        &sz,
        MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER
    ) == nt::STATUS_SUCCESS;
}

/// Reserve a placeholder region of the given size.
/// Returns the base address on success, or nullptr on failure.
[[ nodiscard ]]
inline std::byte * reserve_placeholder( std::size_t const size ) noexcept
{
    PVOID  addr{ nullptr };
    SIZE_T sz  { size };
    if
    (
        nt::NtAllocateVirtualMemoryEx
        (
            nt::current_process,
            &addr,
            &sz,
            std::to_underlying( allocation_type::reserve ) | MEM_RESERVE_PLACEHOLDER,
            PAGE_NOACCESS,
            nullptr, 0
        ) != nt::STATUS_SUCCESS
    )
    {
        return nullptr;
    }
    return static_cast<std::byte *>( addr );
}

/// Replace a placeholder with committed memory (PAGE_READWRITE).
/// Returns true on success.
[[ nodiscard ]]
inline bool commit_placeholder( void * const address, std::size_t const size, allocation_type const alloc_type = allocation_type::commit ) noexcept
{
    PVOID  addr{ address };
    SIZE_T sz  { size };
    return nt::NtAllocateVirtualMemoryEx
    (
        nt::current_process,
        &addr,
        &sz,
        std::to_underlying( alloc_type ) | std::to_underlying( allocation_type::reserve ) | MEM_REPLACE_PLACEHOLDER,
        PAGE_READWRITE,
        nullptr, 0
    ) == nt::STATUS_SUCCESS;
}

/// Commit pages within a SEC_RESERVE section view.
/// NtMapViewOfSectionEx (unlike NtMapViewOfSection) has no CommitSize parameter,
/// so SEC_RESERVE section pages must be explicitly committed after mapping.
[[ nodiscard ]]
inline bool commit_view_pages( void * const address, std::size_t const size, ULONG const page_protection ) noexcept
{
    PVOID  addr{ address };
    SIZE_T sz  { size };
    return nt::NtAllocateVirtualMemory
    (
        nt::current_process,
        &addr,
        0,
        &sz,
        MEM_COMMIT,
        page_protection
    ) == nt::STATUS_SUCCESS;
}

// ----------------------------------------------------------------------------

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
    auto * const adj{ address + current_size };
    auto const placeholder_size{ query_placeholder( adj ) };
    if ( placeholder_size < additional_size )
        return false;

    // Split the placeholder if larger than needed.
    if ( placeholder_size > additional_size && !split_placeholder( adj, additional_size ) )
        return false;

    // Replace the (now exactly-sized) placeholder with committed memory.
    return commit_placeholder( adj, additional_size, alloc_type );
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
    auto * const adj{ address + current_size };
    auto const placeholder_size{ query_placeholder( adj ) };
    if ( placeholder_size < additional_size )
        return false;

    // Split the placeholder if larger than needed.
    if ( placeholder_size > additional_size && !split_placeholder( adj, additional_size ) )
        return false;

    // Map the section view into the placeholder via NtMapViewOfSectionEx.
    PVOID         view_base{ adj };
    LARGE_INTEGER offset { .QuadPart = static_cast<LONGLONG>( current_size ) };
    SIZE_T        view_size{ additional_size };

    if
    (
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
        ) != nt::STATUS_SUCCESS
    )
        return false;

    // NtMapViewOfSectionEx has no CommitSize — commit SEC_RESERVE pages explicitly.
    return commit_view_pages( adj, additional_size, page_protection );
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
    HANDLE        const section_handle,
    std::size_t   const desired_size,
    std::uint64_t const section_offset,
    ULONG         const page_protection
) noexcept
{
    auto const aligned_size { align_up( desired_size, reserve_granularity ) };
    auto const headroom_size{ align_up( aligned_size * std::size_t{ 2 }, reserve_granularity ) };

    // Step 1: reserve a full placeholder region.
    auto * const base{ reserve_placeholder( headroom_size ) };
    if ( !base )
        return nullptr;

    // Step 2: split [aligned_size placeholder | headroom placeholder].
    if ( !split_placeholder( base, aligned_size ) )
    {
        free_placeholder( base );
        return nullptr;
    }

    // Step 3: map section view into the first placeholder.
    PVOID         view_base{ base };
    LARGE_INTEGER nt_offset{ .QuadPart = static_cast<LONGLONG>( section_offset ) };
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
        free_placeholder( base );
        free_placeholder( base + aligned_size );
        return nullptr;
    }

    // NtMapViewOfSectionEx has no CommitSize — commit SEC_RESERVE pages explicitly.
    if ( !commit_view_pages( base, aligned_size, page_protection ) )
    {
        nt::NtUnmapViewOfSectionEx( nt::current_process, base, 0 );
        free_placeholder( base + aligned_size );
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
    std::size_t     const target_size,
    allocation_type const alloc_type = allocation_type::commit
) noexcept
{
    auto const headroom_size{ align_up( target_size * std::size_t{ 2 }, reserve_granularity ) };
    auto * const base{ reserve_placeholder( headroom_size ) };
    if ( !base )
        return nullptr;

    // Split: [target_size placeholder | headroom placeholder]
    if ( !split_placeholder( base, target_size ) )
    {
        // Split failed — free the entire placeholder
        free_placeholder( base );
        return nullptr;
    }

    // Replace the first placeholder with committed memory
    if ( !commit_placeholder( base, target_size, alloc_type ) )
    {
        // Replace failed — free both placeholders
        free_placeholder( base );
        free_placeholder( base + target_size );
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
