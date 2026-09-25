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
/// target_size bytes which starts at the same offset within a pmd_span as the
/// source does.
///
/// The kernel can move a huge page (PMD) mapping only as a whole entry, which
/// requires the source and the destination to sit at the same offset within a
/// pmd_span - at any other offset it first splits every one of them into small
/// page (PTE) mappings (move_page_tables()). Left to choose the destination
/// itself, mremap( MREMAP_MAYMOVE ) keeps no such phase, so a THP backed range
/// that grows by relocation would lose its huge pages on every move: only what
/// is faulted in after the last one would stay huge. A destination cut from a
/// reservation one pmd_span larger than needed can always match the phase.
[[ nodiscard ]] PSI_COLD
inline mremap_result mremap_keeping_pmd_phase( void * const address, std::size_t const current_size, std::size_t const target_size ) noexcept
{
    auto const target_span     { align_up( target_size, std::size_t{ page_size } ) };
    auto const reservation_size{ target_span + pmd_span };
    auto * const reservation{ static_cast<std::byte *>( ::mmap( nullptr, reservation_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0 ) ) };
    if ( reservation == MAP_FAILED ) [[ unlikely ]]
        return { MAP_FAILED };
    // Computed modulo 2^N first, which pmd_span divides: a negative
    // difference wraps around to the same residue.
    auto const   phase      { ( reinterpret_cast<std::uintptr_t>( address ) - reinterpret_cast<std::uintptr_t>( reservation ) ) % pmd_span };
    auto * const destination{ reservation + phase };
    auto * const tail       { destination + target_span };
    auto const   tail_size  { pmd_span - phase }; // never 0: both addresses are page aligned
    // MREMAP_FIXED replaces what is mapped at the destination - here nothing
    // but the reservation's own placeholder pages.
    auto const moved{ ::mremap( address, current_size, target_size, MREMAP_MAYMOVE | MREMAP_FIXED, destination ) };
    // Hand back the reservation's slack on either side of the destination.
    // On a failure the destination itself is deliberately left alone: the
    // kernel may already have unmapped it, at which point it is no longer
    // ours to unmap - leaking a PROT_NONE placeholder on an ENOMEM path beats
    // unmapping whatever another thread might have mapped there since.
    if ( phase )
    {
        BOOST_VERIFY( ::munmap( reservation, phase ) == 0 );
    }
    BOOST_VERIFY( ::munmap( tail, tail_size ) == 0 );
    return { moved };
}

/// Linux mremap: atomic in-place or relocating expansion.
/// Preserves MAP_PRIVATE (COW) pages transparently.
///
/// \param realloc_type  moveable → MREMAP_MAYMOVE (may relocate, preferably
///                                 keeping the PMD phase - see
///                                 mremap_keeping_pmd_phase());
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
        if ( auto const moved{ mremap_keeping_pmd_phase( address, current_size, target_size ) } ) [[ likely ]]
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
