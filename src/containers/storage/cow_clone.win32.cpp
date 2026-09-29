////////////////////////////////////////////////////////////////////////////////
///
/// \file cow_clone.win32.cpp
/// -------------------------
///
/// Windows COW copy constructor for mem_mapping.
/// Duplicates the section handle and maps a PAGE_WRITECOPY view — physical
/// pages are shared until either side writes, at which point the kernel
/// creates private copies. A source that is itself a clone is copied instead.
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include <psi/vm/containers/vm_vector.hpp>
#include <psi/vm/allocators/allocator_base.hpp> // detail::throw_bad_alloc

#include <psi/vm/align.hpp>
#include <psi/vm/detail/nt.hpp>
#include <psi/vm/handles/handle.hpp>
#include <psi/vm/mappable_objects/file/handle.hpp>

#include "../../allocation/expand_win32.hpp"

#include <psi/build/attributes.hpp>

#include <algorithm> // max, min
#include <cstring>   // memcpy, memset
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace
{
    // The address space a clone reserves past its view to grow into in place
    // (at least as much again as the view itself): only address space, but
    // once it runs out the clone has to move into memory of its own.
    std::size_t constexpr growth_headroom{ std::size_t{ 256 } << 20 };
} // anonymous namespace

PSI_COLD
mem_mapping::mem_mapping( mem_mapping const & source )
{
    if ( !source.has_attached_storage() )
        return;

    auto const total_mapped{ source.mapped_size() };
    if ( !total_mapped )
        return;

    // A COW clone copies the source's CURRENT bytes, so it inherits the
    // source's LIVE length (not the source's last committed one). Seeded here,
    // ahead of the strategy branches below, so every success path gets it.
    live_size_ = source.live_size_;

    // A source that is itself a clone keeps its own writes in private pages
    // that are in no section a second view could map: copy what it shows.
    if ( source.views_privately() ) [[ unlikely ]]
    {
        if ( !map( {}, total_mapped ) ) [[ unlikely ]]
            detail::throw_bad_alloc();
        std::memcpy( view_.data(), source.view_.data(), total_mapped );
        return;
    }

    // Duplicate file handle if source is file-backed (for correct
    // is_file_based() reporting and allocation_type in view mapping)
    file_handle cow_file;
    if ( source.mapping_.is_file_based() )
        cow_file = file_handle{ win32::handle_traits::copy( source.mapping_.file.get() ) };

    // Duplicate the section handle (kernel COW: new view gets PAGE_WRITECOPY).
    handle_traits::native_t const dup_section{ win32::handle_traits::copy( source.mapping_.get() ) };

    flags::viewing const cow_view_flags{ PAGE_WRITECOPY };
    mapping_ = { dup_section, cow_view_flags, source.mapping_.ap, std::move( cow_file ) };

    // The view spans the source's header and live bytes only: what the
    // section holds past them (the source's spare capacity) is the source's
    // to fill, and never shows through as the clone's. It is mapped at the
    // head of a placeholder reservation, into which the clone grows with
    // memory of its own (grow_privately()): a view cannot move, nor ever
    // grow by mapping more of the section.
    auto const view_size       { source.get_sizes().data_offset + live_size_ };
    auto const object_extent   { align_up( view_size, std::size_t{ reserve_granularity } ) };
    auto const section_size    { static_cast<std::size_t>( get_size( mapping_ ) ) };
    auto const reservation_size{ object_extent + std::max( object_extent, growth_headroom ) };
    BOOST_ASSUME( view_size <= total_mapped );
    if
    (
        auto * const base{ ( section_size >= object_extent ) ? detail::reserve_placeholder( reservation_size ) : nullptr };
        // (the source's header and live bytes are committed: it uses them)
        base && detail::reserved_section_map( mapping_.get(), base, reservation_size, object_extent, cow_view_flags.page_protection, view_size )
    ) [[ likely ]]
    {
        view_          = extendable_mapped_view::adopt( { base, view_size }, reservation_size - object_extent );
        object_extent_ = object_extent;
    }
    else
    {
        // A file section that ends short of the view's last allocation granule
        // (or no room for the reservation): a plain view, in whose granule the
        // pages past the section cannot be touched and nothing else can be
        // mapped - the clone moves into memory of its own on its first growth
        // past the section.
        view_          = extendable_mapped_view::map( mapping_, cow_view_flags, 0, view_size ); // fallible_result throws on error
        object_extent_ = std::min( object_extent, align_up( section_size, std::size_t{ commit_granularity } ) );
    }
}

////////////////////////////////////////////////////////////////////////////////
// Growth and shrinkage of a private view (a COW clone):
// [ object part: PAGE_WRITECOPY view of the shared section | tail: private
// memory committed into the view's trailing placeholder ].
////////////////////////////////////////////////////////////////////////////////

bool mem_mapping::grow_privately( std::size_t const target_size )
{
    auto * const base{ view_.data() };
    auto   const size{ view_.size() };
    // the end of what is mapped (and accessible): the object part, or the
    // last whole allocation granule of the tail past it
    auto const mapped_end{ ( size > object_extent_ ) ? align_up( size, std::size_t{ reserve_granularity } ) : object_extent_ };
    BOOST_ASSUME( target_size > size );
    // What is already mapped past the view's end (views are mapped in whole
    // allocation granules) reads as zero once it is part of the view, as
    // fresh growth does. Where that is the section's this also makes it the
    // clone's own (private copies), so that what the source writes there
    // later does not show through.
    std::memset( base + size, 0, std::min( target_size, mapped_end ) - size );
    if ( target_size <= mapped_end )
    {
        static_cast<mapped_span &>( view_ ) = { base, target_size };
        return true;
    }
    // a tail can only start on an allocation granule
    if ( !is_aligned( mapped_end, std::size_t{ reserve_granularity } ) )
        return false;
    return view_.expand_privately( target_size );
}

void mem_mapping::shrink_privately( std::size_t const target_size ) noexcept( mapping::views_downsizeable )
{
    // A section view can neither be cut short nor mapped afresh without
    // dropping the clone's writes: the clone moves into memory of its own.
    if ( view_.size() != target_size )
        move_into_memory( target_size );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
