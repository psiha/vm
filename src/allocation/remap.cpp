////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2023 - 2026.
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
#include "allocation.impl.hpp"
#include <psi/vm/align.hpp>
#include <psi/vm/allocation.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>

#ifdef _WIN32
#   include <psi/vm/detail/nt.hpp>
#elif defined( __linux__ )
#   ifndef _GNU_SOURCE
#       define _GNU_SOURCE
#   endif
#   include <sys/mman.h>
#elif defined( __APPLE__ )
#   include <mach/mach_init.h>
#   include <mach/mach_vm.h>
#   include <mach/vm_statistics.h>
#   include <sys/mman.h>
#endif // win32 / linux / apple

#ifndef NDEBUG
#include <cerrno>
#endif
#include <cstddef>
#include <cstring> // memcpy
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

[[ gnu::cold ]]
expand_result expand
(
    std::byte         *       address,
    std::size_t         const current_size,
    std::size_t         const required_size_for_end_expansion,
    std::size_t         const required_size_for_front_expansion,
    std::size_t         const used_capacity,
    allocation_type     const alloc_type,
    reallocation_type   const realloc_type
) noexcept
{
    BOOST_ASSUME( address != nullptr    );
#ifndef _WIN32
    BOOST_ASSUME( address != MAP_FAILED );
#endif

    BOOST_ASSUME( current_size >  0                               ); // otherwise we should have never gotten here
    BOOST_ASSUME( current_size <  required_size_for_end_expansion );
    BOOST_ASSUME( current_size >= used_capacity                   );

    BOOST_ASSUME( is_aligned( address                          , reserve_granularity ) );
    BOOST_ASSUME( is_aligned( current_size                     , reserve_granularity ) );
    BOOST_ASSUME( is_aligned( required_size_for_end_expansion  , reserve_granularity ) );
    BOOST_ASSUME( is_aligned( required_size_for_front_expansion, reserve_granularity ) );

    BOOST_ASSUME( ( alloc_type == allocation_type::commit ) || !used_capacity );

    // First simply try to append or prepend the required extra space

    // - append
    if ( required_size_for_end_expansion )
    {
        BOOST_ASSUME( required_size_for_end_expansion > current_size );
#   if defined( __linux__ )
        // mremap requires the same protection for the entire range (TODO: add this as a precondition for calling code?)
        // https://github.com/torvalds/linux/blob/master/mm/mremap.c
        // TODO: implement 'VirtualQuery4Linux' https://github.com/ouadev/proc_maps_parser
        BOOST_VERIFY( ::mprotect( address, current_size, static_cast<int>( allocation_type::commit ) ) == 0 );

        // https://stackoverflow.com/questions/11621606/faster-way-to-move-memory-page-than-mremap
        auto const actual_address{ ::mremap( address, current_size, required_size_for_end_expansion, static_cast<int>( realloc_type ) ) }; // https://laptrinhx.com/linux-mremap-tlb-flush-too-late-1728576753
        if ( actual_address != MAP_FAILED ) [[ likely ]]
        {
            auto remap_method{ expand_result::moved };
            if ( realloc_type == reallocation_type::fixed )
            {
                BOOST_ASSUME( actual_address == address );
                remap_method = expand_result::back_extended;
            }
            return { { static_cast< std::byte * >( actual_address ), required_size_for_end_expansion }, remap_method };
        }
        else
        {
            BOOST_ASSERT_MSG( ( errno == ENOMEM ) && ( realloc_type == reallocation_type::fixed ), "Unexpected mremap failure" );
        }
#   elif defined( __APPLE__ )
        // Try mach_vm_remap: remap existing pages to a new (larger) allocation
        // with copy=FALSE (zero-copy, shares physical pages) then deallocate old region.
        if ( realloc_type == reallocation_type::moveable )
        {
            // Allocate a new region of the required size
            mach_vm_address_t new_addr{ 0 };
            auto kr{ ::mach_vm_allocate( ::mach_task_self(), &new_addr, required_size_for_end_expansion, VM_FLAGS_ANYWHERE ) };
            if ( kr == KERN_SUCCESS )
            {
                // Remap the old pages into the beginning of the new region (zero-copy)
                vm_prot_t cur_prot;
                vm_prot_t max_prot;
                kr = ::mach_vm_remap
                (
                    ::mach_task_self(),                                         // target task
                    &new_addr,                                                  // target address (overwritten in-place)
                    current_size,                                               // size to remap
                    0,                                                          // alignment mask (page-aligned)
                    VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,                        // overwrite the region we just allocated
                    ::mach_task_self(),                                         // source task
                    reinterpret_cast<mach_vm_address_t>( address ),             // source address
                    FALSE,                                                      // copy=FALSE: move/share pages (zero-copy)
                    &cur_prot,                                                  // out: current protection
                    &max_prot,                                                  // out: max protection
                    VM_INHERIT_NONE                                             // inheritance
                );
                if ( kr == KERN_SUCCESS )
                {
                    // Deallocate old region (pages were remapped, old VA range is stale)
                    ::mach_vm_deallocate( ::mach_task_self(), reinterpret_cast<mach_vm_address_t>( address ), current_size );
                    return { { reinterpret_cast<std::byte *>( new_addr ), required_size_for_end_expansion }, expand_result::moved };
                }
                // remap failed: free the speculative allocation and fall through
                ::mach_vm_deallocate( ::mach_task_self(), new_addr, required_size_for_end_expansion );
            }
        }
#   endif // linux mremap / apple mach_vm_remap
        auto const additional_end_size{ required_size_for_end_expansion - current_size };
#   ifdef _WIN32
        // Windows placeholder-based in-place expansion.
        // If a previous over-reserving expand() left a trailing placeholder,
        // split+replace it for guaranteed in-place growth with no memcpy.
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
                ) == nt::STATUS_SUCCESS
                && mbi.State      == MEM_RESERVE
                && mbi.RegionSize >= additional_end_size
            )
            {
                auto const placeholder_size{ static_cast<std::size_t>( mbi.RegionSize ) };
                auto * const adj{ address + current_size };
                bool can_replace{ true };

                // If the placeholder is larger than needed, split it first.
                // NtFreeVirtualMemory(MEM_PRESERVE_PLACEHOLDER) only succeeds on
                // actual placeholders — acts as an implicit type check.
                if ( placeholder_size > additional_end_size )
                {
                    PVOID  split_addr{ adj };
                    SIZE_T split_sz  { additional_end_size };
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

                if ( can_replace )
                {
                    // Replace the (now exactly-sized) placeholder with committed/reserved memory
                    PVOID  replace_addr{ adj };
                    SIZE_T replace_sz  { additional_end_size };
                    if
                    (
                        nt::NtAllocateVirtualMemoryEx
                        (
                            nt::current_process,
                            &replace_addr,
                            &replace_sz,
                            std::to_underlying( alloc_type ) | std::to_underlying( allocation_type::reserve ) | MEM_REPLACE_PLACEHOLDER,
                            PAGE_READWRITE,
                            nullptr, 0
                        ) == nt::STATUS_SUCCESS
                    )
                    {
                        return { { address, required_size_for_end_expansion }, expand_result::back_extended };
                    }
                    // Replace failed (OOM?) — the split placeholder is orphaned (VA-only cost).
                }
            }
        }
#   endif // _WIN32 placeholder expansion
        if ( allocate_fixed( address + current_size, additional_end_size, alloc_type ) )
        {
#       if defined( __linux__ )
            BOOST_ASSERT_MSG( false, "mremap failed but an appending mmap succeeded!?" ); // behaviour investigation
#       endif
            BOOST_ASSUME( current_size + additional_end_size == required_size_for_end_expansion );
            return { { address, required_size_for_end_expansion }, expand_result::method::back_extended };
        }
    }

    // - prepend
    if ( required_size_for_front_expansion )
    {
        BOOST_ASSUME( required_size_for_front_expansion > current_size );
        auto const additional_front_size{ required_size_for_front_expansion - current_size };
        void * pre_address{ address - additional_front_size };
        if ( allocate_fixed( pre_address, additional_front_size, allocation_type::commit ) ) // avoid having a non-committed range before a committed range
        {
            BOOST_VERIFY( current_size + additional_front_size == required_size_for_front_expansion );
            return { { static_cast< std::byte * >( pre_address ), required_size_for_front_expansion }, expand_result::method::front_extended };
        }
    }

    if ( realloc_type == reallocation_type::moveable )
    {
#   ifdef _WIN32
        // Windows: over-reserve with trailing placeholder.
        // The headroom placeholder enables future expand() calls to extend
        // in-place via the split+replace path above — avoiding memcpy on
        // subsequent growths (analogous to Linux mremap).
        if ( required_size_for_end_expansion )
        {
            auto const headroom_size{ align_up( required_size_for_end_expansion * std::size_t{ 2 }, reserve_granularity ) };
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
                ) == nt::STATUS_SUCCESS
            )
            {
                auto * const base{ static_cast<std::byte *>( ph_addr ) };

                // Split: [required_size placeholder | headroom placeholder]
                PVOID  split_addr{ ph_addr };
                SIZE_T split_sz  { required_size_for_end_expansion };
                if
                (
                    nt::NtFreeVirtualMemory
                    (
                        nt::current_process,
                        &split_addr,
                        &split_sz,
                        MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER
                    ) == nt::STATUS_SUCCESS
                )
                {
                    // Replace the first placeholder with committed memory
                    PVOID  commit_addr{ base };
                    SIZE_T commit_sz  { required_size_for_end_expansion };
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
                        ) == nt::STATUS_SUCCESS
                    )
                    {
                        // Trailing headroom placeholder at base+required_size remains
                        // for future in-place expansion via the split+replace path.
                        std::memcpy( base, address, used_capacity );
                        free( address, current_size );
                        return { { base, required_size_for_end_expansion }, expand_result::moved };
                    }

                    // Replace failed — free the split placeholder
                    PVOID  free_addr{ base };
                    SIZE_T free_sz  { 0 };
                    nt::NtFreeVirtualMemory( nt::current_process, &free_addr, &free_sz, MEM_RELEASE );
                }

                // Free the remaining (or unsplit) placeholder
                auto * tail{ base + required_size_for_end_expansion };
                PVOID  tail_addr{ tail };
                SIZE_T tail_sz  { 0 };
                nt::NtFreeVirtualMemory( nt::current_process, &tail_addr, &tail_sz, MEM_RELEASE );
            }
        }
#   endif // _WIN32 placeholder over-reserve

        // Generic fallback: allocate new->copy->free old dance.
        auto       requested_size{ required_size_for_end_expansion }; //...mrmlj...TODO respect front-expand-only requests
        auto const new_location  { allocate( requested_size )      };
        if ( new_location )
        {
            BOOST_ASSUME( requested_size == required_size_for_end_expansion );
            std::memcpy( new_location, address, used_capacity );
            free( address, current_size );
            return { { static_cast< std::byte * >( new_location ), required_size_for_end_expansion }, expand_result::method::moved };
        }
    }

    return {};
}

expand_result expand_back
(
    std::byte *       const address,
    std::size_t       const current_size,
    std::size_t       const required_size,
    std::size_t       const used_capacity,
    allocation_type   const alloc_type,
    reallocation_type const realloc_type
) noexcept
{
    return expand( address, current_size, required_size, 0, used_capacity, alloc_type, realloc_type );
}

expand_result expand_front
(
    std::byte *       const address,
    std::size_t       const current_size,
    std::size_t       const required_size,
    std::size_t       const used_capacity,
    allocation_type   const alloc_type,
    reallocation_type const realloc_type
) noexcept
{
    return expand( address, current_size, 0, required_size, used_capacity, alloc_type, realloc_type );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
