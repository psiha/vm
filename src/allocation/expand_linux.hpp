////////////////////////////////////////////////////////////////////////////////
///
/// \file expand_linux.hpp
/// ----------------------
///
/// Shared Linux expand primitive: mremap wrapper.
///
/// Copyright (c) Domagoj Saric 2023 - 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once
#ifdef __linux__
#include <sys/mman.h>

#include <psi/vm/align.hpp>
#include <psi/vm/allocation.hpp> // page_size, reallocation_type

#include <psi/build/attributes.hpp>

#include <boost/assert.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm::detail
{
//------------------------------------------------------------------------------

struct mremap_result
{
    void * address;
    explicit operator bool() const noexcept { return address != MAP_FAILED; }
};

/// The span one PMD (the page table level above the last) entry maps, i.e.
/// the size of a transparent huge page: a page table page of 64 bit entries
/// holds page_size / 8 of them, so with the 4 KiB granule this is 2 MiB.
inline std::size_t constexpr pmd_span{ std::size_t{ page_size } * ( page_size / sizeof( std::uint64_t ) ) };

/// Relocates [address, address + current_size) to a fresh range of
/// target_size bytes which starts at the given offset (phase) within a
/// pmd_span, cut from a reservation one pmd_span larger than needed (which
/// can always match any phase). Leaves the source untouched on failure.
///
/// Callers pick the phase by what the kernel needs to map (or keep) huge
/// pages there:
///  - a growing mapping keeps the phase it has (linux_mremap()): the kernel
///    moves a huge page (PMD) mapping only as a whole entry, which requires
///    the source and the destination to sit at the same offset within a
///    pmd_span - at any other offset it first splits every one of them into
///    small page (PTE) mappings (move_page_tables()), and mremap(
///    MREMAP_MAYMOVE ) left to choose the destination itself keeps no such
///    phase;
///  - a file view takes the phase of its file offset
///    (place_file_view_at_offset_phase()).
[[ nodiscard ]] PSI_COLD
inline mremap_result mremap_to_pmd_phase( void * const address, std::size_t const current_size, std::size_t const target_size, std::uintptr_t const phase ) noexcept
{
    auto const target_span     { align_up( target_size, std::size_t{ page_size } ) };
    auto const reservation_size{ target_span + pmd_span };
    auto * const reservation{ static_cast<std::byte *>( ::mmap( nullptr, reservation_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0 ) ) };
    if ( reservation == MAP_FAILED ) [[ unlikely ]]
        return { MAP_FAILED };
    // Computed modulo 2^N first, which pmd_span divides: a negative
    // difference wraps around to the same residue.
    auto const   head       { ( phase - reinterpret_cast<std::uintptr_t>( reservation ) ) % pmd_span };
    auto * const destination{ reservation + head };
    auto * const tail       { destination + target_span };
    auto const   tail_size  { pmd_span - head }; // never 0: both addresses are page aligned
    // MREMAP_FIXED replaces what is mapped at the destination - here nothing
    // but the reservation's own placeholder pages.
    auto const moved{ ::mremap( address, current_size, target_size, MREMAP_MAYMOVE | MREMAP_FIXED, destination ) };
    // Hand back the reservation's slack on either side of the destination.
    // On a failure the destination itself is deliberately left alone: the
    // kernel may already have unmapped it, at which point it is no longer
    // ours to unmap - leaking a PROT_NONE placeholder on an ENOMEM path beats
    // unmapping whatever another thread might have mapped there since.
    if ( head )
    {
        BOOST_VERIFY( ::munmap( reservation, head ) == 0 );
    }
    BOOST_VERIFY( ::munmap( tail, tail_size ) == 0 );
    return { moved };
}

/// Moves a file (incl. memfd/shmem) view that the kernel has just placed
/// [address, address + size), mapping the file from offset on, to an address
/// congruent to that offset modulo pmd_span, and returns where the view now
/// is: address itself if it already is congruent, if the view is not a file
/// view (file_handle -1) or if the move fails (the view stays valid there).
///
/// The kernel maps a large folio of a file (page cache or shmem) only where
/// the virtual address and the file offset agree modulo the folio size
/// (thp_vma_suitable_order()), and it aligns a file mapping so on its own
/// only when the mapping is at least a pmd_span long when created. A view
/// that starts smaller and then grows - in place, or by a relocation that
/// keeps its phase (linux_mremap()) - would therefore never get a huge
/// folio. The view is mapped at a place of the kernel's choosing first and
/// only then moved, so that a failing mmap (e.g. a protection the file
/// descriptor does not permit) never leaves a reservation behind.
[[ nodiscard ]] PSI_COLD
inline void * place_file_view_at_offset_phase( void * const address, std::size_t const size, int const file_handle, std::uint64_t const offset ) noexcept
{
#ifndef __ANDROID__ // server Linux
    auto const phase{ static_cast<std::uintptr_t>( offset % pmd_span ) };
    if ( ( file_handle != -1 ) && ( reinterpret_cast<std::uintptr_t>( address ) % pmd_span != phase ) )
    {
        if ( auto const moved{ mremap_to_pmd_phase( address, size, size, phase ) } ) [[ likely ]]
            return moved.address;
    }
#else
    (void)size; (void)file_handle; (void)offset;
#endif
    return address;
}

/// Linux mremap: atomic in-place or relocating expansion.
/// Preserves MAP_PRIVATE (COW) pages transparently.
///
/// \param realloc_type  moveable → MREMAP_MAYMOVE (may relocate, preferably
///                                 keeping the PMD phase - see
///                                 mremap_to_pmd_phase());
///                      fixed    → in-place only (fails if not possible)
[[ nodiscard ]] PSI_COLD
inline mremap_result linux_mremap(
    void            * const address,
    std::size_t       const current_size,
    std::size_t       const target_size,
    reallocation_type const realloc_type = reallocation_type::moveable
) noexcept
{
    if ( realloc_type == reallocation_type::moveable )
    {
        // In place first, exactly as MREMAP_MAYMOVE would: nothing moves, so
        // there is no phase to keep.
        if ( auto const in_place{ ::mremap( address, current_size, target_size, 0 ) }; in_place != MAP_FAILED )
            return { in_place };
        BOOST_ASSERT_MSG( errno == ENOMEM, "Unexpected mremap failure" );
        if ( auto const moved{ mremap_to_pmd_phase( address, current_size, target_size, reinterpret_cast<std::uintptr_t>( address ) % pmd_span ) } ) [[ likely ]]
            return moved;
        // no room for the reservation (or the move into it failed): let the kernel place it
    }
    auto const result{ ::mremap( address, current_size, target_size, static_cast<int>( realloc_type ) ) };
    if ( result == MAP_FAILED )
    {
        BOOST_ASSERT_MSG( ( errno == ENOMEM ) && ( realloc_type == reallocation_type::fixed ), "Unexpected mremap failure" );
    }
    return { result };
}

//------------------------------------------------------------------------------
} // namespace psi::vm::detail
//------------------------------------------------------------------------------
#endif // __linux__
