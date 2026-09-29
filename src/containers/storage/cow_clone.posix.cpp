////////////////////////////////////////////////////////////////////////////////
///
/// \file cow_clone.posix.cpp
/// -------------------------
///
/// POSIX COW copy constructor for mem_mapping:
/// - File-backed (all): dup(fd) + MAP_PRIVATE view (kernel COW)
/// - Anonymous macOS: mach_vm_remap(copy=TRUE) + memcpy fallback
/// - Anonymous Linux: memfd_create + memcpy into a memfd of the clone's own,
///                    mapped shared as map_cow_memory() maps one (so clones
///                    of the clone are true COW), or plain memcpy fallback
/// A source that is itself a clone (a private view) takes the anonymous path:
/// its own writes are in no object the fd refers to.
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
#include <psi/vm/handles/handle.hpp>

#include <boost/assert.hpp>

#ifdef __APPLE__
#   include "../../detail/mach.hpp"
#endif

#include <sys/mman.h>
#ifdef __linux__
#   include "../../allocation/expand_linux.hpp" // pmd_span
#   if __has_include( <sys/memfd.h> )
#       include <sys/memfd.h>
#   endif
#   ifndef MFD_CLOEXEC
#       define MFD_CLOEXEC 0x0001U
        extern "C" int memfd_create( char const *, unsigned int ) noexcept;
#   endif
#endif

#include <unistd.h>

#include <algorithm> // min
#include <cerrno>
#include <cstdint>
#include <cstdlib> // abort
#include <cstring> // memcpy, memset
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// mem_mapping copy constructor (COW clone)
//
// Creates a copy-on-write clone: physical pages are shared with the source
// until either side writes, at which point the kernel creates private copies.
////////////////////////////////////////////////////////////////////////////////

PSI_COLD
mem_mapping::mem_mapping( mem_mapping const & source )
    :
    mem_mapping{}
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

    // fd-backed (real file or memfd): dup + MAP_PRIVATE gives kernel COW -
    // provided the source's view shows what the fd holds. A source that is
    // itself a clone keeps its own writes in private pages no second view of
    // the fd could see, so it is copied like anonymous memory instead.
    if ( source.mapping_.has_fd() && !source.views_privately() )
    {
        // handle_traits::copy returns fallible_result — auto-throws on error
        posix::handle_traits::native_t const cow_fd{ posix::handle_traits::copy( source.mapping_.get() ) };

        flags::viewing const cow_view_flags
        {
            .protection = PROT_READ | PROT_WRITE,
            .flags      = MAP_PRIVATE
        };
        // The view spans the source's header and live bytes only: what the
        // object holds past them (the source's spare capacity) is the
        // source's to fill, and never shows through as the clone's - the
        // clone grows past its view with memory of its own (grow_privately()).
        auto const view_size{ source.get_sizes().data_offset + live_size_ };
        BOOST_ASSUME( view_size <= total_mapped );
        mapping_ = { posix::handle{ cow_fd }, cow_view_flags, view_size };
#   ifdef __linux__
        if ( !source.mapping_.is_file_based() ) // source is ephemeral (memfd)
            mapping_.set_ephemeral();
#   endif
        view_          = extendable_mapped_view::map( mapping_, cow_view_flags, 0, view_size );
        object_extent_ = align_up( view_size, page_size );
        return;
    }

    // Anonymous (no fd), or a private view: platform-specific strategies, each
    // with a memcpy fallback.

#if defined( __APPLE__ )
    {
        // macOS: try mach_vm_remap(copy=TRUE) for zero-copy COW.
        // Allocate an anonymous target, then replace its pages with COW copies.
        flags::viewing const cow_view_flags
        {
            .protection = PROT_READ | PROT_WRITE,
            .flags      = MAP_PRIVATE | MAP_ANONYMOUS
        };
        mapping_ = { posix::handle{}, cow_view_flags, total_mapped };
        view_    = extendable_mapped_view::map( mapping_, cow_view_flags, 0, total_mapped );

        auto target_addr{ reinterpret_cast<mach_vm_address_t>( view_.data() ) };
        auto const kr
        {
            mach::vm_remap_overwrite
            (
                &target_addr,
                total_mapped,
                source.view_.data(),
                TRUE, // copy = TRUE -> COW
                VM_INHERIT_COPY
            )
        };
        if ( kr != KERN_SUCCESS ) [[ unlikely ]]
            std::memcpy( view_.data(), source.view_.data(), total_mapped ); // fallback: deep copy
        return;
    }

#elif defined( __linux__ )
    {
        // Linux: copy into a memfd of the clone's own - set up as
        // map_cow_memory() sets one up: the clone is its only holder, so it
        // maps it shared and may resize it, while clones of this clone map it
        // privately (dup + MAP_PRIVATE above) for true kernel COW.
        auto const mfd{ ::memfd_create( "psi_vm_cow", MFD_CLOEXEC ) };
        if ( mfd != -1 )
        {
            if ( ::ftruncate( mfd, static_cast<off_t>( total_mapped ) ) != 0 ) [[ unlikely ]]
            {
                BOOST_ASSERT( errno == ENOMEM || errno == ENOSPC );
                ::close( mfd );
                detail::throw_bad_alloc();
            }
            if ( !map( file_handle{ mfd }, total_mapped ) ) [[ unlikely ]]
            {
                mapping_.close();
                detail::throw_bad_alloc();
            }
            mapping_.set_ephemeral(); // memfd: fd-backed but not on-disk
            std::memcpy( view_.data(), source.view_.data(), total_mapped );
            return;
        }
        // memfd_create failed (fd limit or kernel too old) — fall through to memcpy
    }
#endif

    // Universal fallback: allocate anonymous mapping + deep copy
    flags::viewing const cow_view_flags
    {
        .protection = PROT_READ | PROT_WRITE,
        .flags      = MAP_PRIVATE | MAP_ANONYMOUS
    };
    mapping_ = { posix::handle{}, cow_view_flags, total_mapped };
    view_    = extendable_mapped_view::map( mapping_, cow_view_flags, 0, total_mapped );
    std::memcpy( view_.data(), source.view_.data(), total_mapped );
}

////////////////////////////////////////////////////////////////////////////////
// Growth and shrinkage of a private view (a COW clone of an fd-backed
// container): [ object part: MAP_PRIVATE view of the shared file/memfd |
// tail: MAP_PRIVATE | MAP_ANONYMOUS ]. Neither ever resizes the object, nor
// maps more of it than the clone was created with.
////////////////////////////////////////////////////////////////////////////////

namespace
{
    int constexpr anonymous{ MAP_PRIVATE | MAP_ANONYMOUS };
    int constexpr rw       { PROT_READ | PROT_WRITE };

    //! Maps anonymous memory exactly at [address, address + size) if that
    //! range is free, never replacing anything already mapped there.
    [[ nodiscard ]] bool map_anonymous_at( std::byte * const address, std::size_t const size ) noexcept
    {
#   ifdef MAP_FIXED_NOREPLACE
        int constexpr noreplace{ MAP_FIXED_NOREPLACE };
#   else
        int constexpr noreplace{ 0 }; // a hint only: checked below
#   endif
        auto * const mapped{ ::mmap( address, size, rw, anonymous | noreplace, -1, 0 ) };
        if ( mapped == address )
            return true;
        // kernels older than MAP_FIXED_NOREPLACE (4.17) take it as a hint,
        // as a system without it takes the address
        if ( mapped != MAP_FAILED )
            BOOST_VERIFY( ::munmap( mapped, size ) == 0 );
        return false;
    }

    //! Grows a private view of mapped_end bytes, object_extent of them the
    //! object part, to target_end bytes where it is. On Linux a tail the view
    //! already has is grown by mremap(), never by mapping more next to it: a
    //! tail that has moved keeps the page offset of where it was created, so
    //! an anonymous mapping made next to it would not merge with it - and a
    //! tail of several mappings could no longer move in one call (relocate()).
    [[ nodiscard ]] bool grow_in_place
    (
        std::byte * const base,
        std::size_t const object_extent,
        std::size_t const mapped_end,
        std::size_t const target_end
    ) noexcept
    {
#   ifdef __linux__
        if ( object_extent != mapped_end )
            return ::mremap( base + object_extent, mapped_end - object_extent, target_end - object_extent, 0 ) != MAP_FAILED;
#   else
        (void)object_extent;
#   endif
        return map_anonymous_at( base + mapped_end, target_end - mapped_end );
    }

#if defined( __linux__ )
    //! Moves a private view [ object part | anonymous tail ] of mapped_end
    //! bytes to a range of target_end bytes (at the same huge page phase, see
    //! mremap_to_pmd_phase()), the part past mapped_end being fresh anonymous
    //! memory, and returns where it now is - or nullptr where it could not be
    //! moved (the view then is still where it was). mremap() moves a single
    //! mapping (VMA) per call, and a file and anonymous memory never share
    //! one, so the view is two: the object part, and the tail, which stays a
    //! single mapping - it is only ever created whole, grown in place by
    //! mremap() (grow_in_place()) and moved and grown by one mremap() here.
    //! The tail takes its place at the destination first, moved and grown
    //! (or, where there is none yet, mapped fresh): that is the step that can
    //! fail, for want of memory for the growth, and nothing has moved then.
    //! The object part follows; that move grows nothing, so it can only fail
    //! where the process has run out of mappings (vm.max_map_count), or where
    //! a caller has split the view's mappings itself (mprotect(), a madvise()
    //! that changes a mapping's flags, mlock() of a part of it): the view is
    //! then half moved, and the process aborts.
    [[ nodiscard ]] std::byte * relocate
    (
        std::byte * const base,
        std::size_t const object_extent,
        std::size_t const mapped_end,
        std::size_t const target_end
    ) noexcept
    {
        auto const reservation_size{ target_end + detail::pmd_span };
        auto * const reservation{ static_cast<std::byte *>( ::mmap( nullptr, reservation_size, PROT_NONE, anonymous | MAP_NORESERVE, -1, 0 ) ) };
        if ( reservation == MAP_FAILED ) [[ unlikely ]]
            return nullptr;
        auto const   head       { ( reinterpret_cast<std::uintptr_t>( base ) - reinterpret_cast<std::uintptr_t>( reservation ) ) % detail::pmd_span };
        auto * const destination{ reservation + head };
        auto * const new_tail   { destination + object_extent };
        auto const   tail_size  { mapped_end - object_extent };
        auto const   tail_placed
        {
            tail_size
                ? ::mremap( base + object_extent, tail_size, target_end - object_extent, MREMAP_MAYMOVE | MREMAP_FIXED, new_tail ) == new_tail
                : ::mmap  ( new_tail, target_end - object_extent, rw, anonymous | MAP_FIXED, -1, 0 )                          == new_tail
        };
        // what the reservation still holds past its first lead bytes and past the view's new end
        auto const release_slack{ [ = ]( std::size_t const lead ) noexcept
        {
            if ( lead )
                BOOST_VERIFY( ::munmap( reservation, lead ) == 0 );
            BOOST_VERIFY( ::munmap( destination + target_end, detail::pmd_span - head ) == 0 );
        } };
        if ( !tail_placed ) [[ unlikely ]]
        {
            // The kernel may already have unmapped the tail's destination (a
            // fixed-address mremap() or mmap() unmaps before it checks), and
            // may then have handed it to another thread: it is no longer ours
            // to unmap - only the rest of the reservation is.
            release_slack( head + object_extent );
            return nullptr;
        }
        if ( ::mremap( base, object_extent, object_extent, MREMAP_MAYMOVE | MREMAP_FIXED, destination ) == MAP_FAILED ) [[ unlikely ]]
            std::abort();
        release_slack( head );
        return destination;
    }
#elif defined( __APPLE__ )
    //! As above, via mach_vm_remap( copy = FALSE ), which moves any number of
    //! mappings in one call (the object part keeps its file backing and the
    //! clone's private copies alike).
    [[ nodiscard ]] std::byte * relocate
    (
        std::byte * const base,
        std::size_t const /*object_extent*/,
        std::size_t const mapped_end,
        std::size_t const target_end
    ) noexcept
    {
        auto * const destination{ static_cast<std::byte *>( ::mmap( nullptr, target_end, rw, anonymous, -1, 0 ) ) };
        if ( destination == MAP_FAILED ) [[ unlikely ]]
            return nullptr;
        auto target_address{ reinterpret_cast<mach_vm_address_t>( destination ) };
        if ( mach::vm_remap_overwrite( &target_address, mapped_end, base, FALSE, VM_INHERIT_COPY ) != KERN_SUCCESS ) [[ unlikely ]]
        {
            BOOST_VERIFY( ::munmap( destination, target_end ) == 0 );
            return nullptr;
        }
        BOOST_VERIFY( ::munmap( base, mapped_end ) == 0 );
        return destination;
    }
#else
    [[ nodiscard ]] std::byte * relocate( std::byte *, std::size_t, std::size_t, std::size_t ) noexcept { return nullptr; }
#endif
} // anonymous namespace

bool mem_mapping::grow_privately( std::size_t const target_size )
{
    auto *     base      { view_.data() };
    auto const size      { view_.size() };
    auto const mapped_end{ align_up( size       , std::size_t{ page_size } ) };
    auto const target_end{ align_up( target_size, std::size_t{ page_size } ) };
    BOOST_ASSUME( target_size > size );
    BOOST_ASSUME( object_extent_ <= mapped_end );
    // What is already mapped past the view's end - its last page - reads as
    // zero once it is part of the view, as fresh growth does. Where that is
    // the object's page this also makes it the clone's own (a private copy),
    // so that what the source writes there later does not show through.
    std::memset( base + size, 0, std::min( target_size, mapped_end ) - size );
    if ( target_end > mapped_end )
    {
        if ( !grow_in_place( base, object_extent_, mapped_end, target_end ) )
        {
            auto * const moved{ relocate( base, object_extent_, mapped_end, target_end ) };
            if ( !moved ) [[ unlikely ]]
                return false;
            base = moved;
        }
    }
    static_cast<mapped_span &>( view_ ) = { base, target_size };
    return true;
}

void mem_mapping::shrink_privately( std::size_t const target_size ) noexcept( mapping::views_downsizeable )
{
    auto * const base      { view_.data() };
    auto   const mapped_end{ align_up( view_.size(), std::size_t{ page_size } ) };
    auto   const target_end{ align_up( target_size , std::size_t{ page_size } ) };
    // Unmapped, never truncated: the object stays the source's, and a
    // regrowth maps fresh memory of the clone's own.
    if ( mapped_end > target_end )
        BOOST_VERIFY( ::munmap( base + target_end, mapped_end - target_end ) == 0 );
    object_extent_ = std::min( object_extent_, target_end );
    static_cast<mapped_span &>( view_ ) = { base, target_size };
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
