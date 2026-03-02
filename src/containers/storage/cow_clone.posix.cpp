////////////////////////////////////////////////////////////////////////////////
///
/// \file cow_clone.posix.cpp
/// -------------------------
///
/// POSIX COW copy constructor for mem_mapping:
/// - File-backed (all): dup(fd) + MAP_PRIVATE view (kernel COW)
/// - Anonymous macOS: mach_vm_remap(copy=TRUE) + memcpy fallback
/// - Anonymous Linux: memfd_create + memcpy + MAP_PRIVATE (true COW)
///                    or plain memcpy fallback
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

#include <psi/vm/handles/handle.hpp>

#include <boost/assert.hpp>

#ifdef __APPLE__
#   include "../../detail/mach.hpp"
#endif

#ifdef __linux__
#   include <sys/mman.h>
#   if __has_include( <sys/memfd.h> )
#       include <sys/memfd.h>
#   endif
#   ifndef MFD_CLOEXEC
#       define MFD_CLOEXEC 0x0001U
        extern "C" int memfd_create( char const *, unsigned int ) noexcept;
#   endif
#endif

#include <unistd.h>

#include <cerrno>
#include <cstring> // memcpy
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

[[ gnu::cold ]]
mem_mapping::mem_mapping( mem_mapping const & source )
    :
    mem_mapping{}
{
    if ( !source.has_attached_storage() )
        return;

    auto const total_mapped{ source.mapped_size() };
    if ( !total_mapped )
        return;

    // fd-backed (real file or memfd): dup + MAP_PRIVATE gives kernel COW.
    if ( source.mapping_.has_fd() )
    {
        // handle_traits::copy returns fallible_result — auto-throws on error
        posix::handle_traits::native_t const cow_fd{ posix::handle_traits::copy( source.mapping_.get() ) };

        flags::viewing const cow_view_flags
        {
            .protection = PROT_READ | PROT_WRITE,
            .flags      = MAP_PRIVATE
        };
        mapping_ = { posix::handle{ cow_fd }, cow_view_flags, total_mapped };
#   ifdef __linux__
        if ( !source.mapping_.is_file_based() ) // source is ephemeral (memfd)
            mapping_.set_ephemeral();
#   endif
        view_ = mapped_view::map( mapping_, cow_view_flags, 0, total_mapped );
        return;
    }

    // Anonymous (no fd): platform-specific strategies, each with a memcpy fallback.

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
        view_    = mapped_view::map( mapping_, cow_view_flags, 0, total_mapped );

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
        // Linux: try memfd_create + memcpy + MAP_PRIVATE for true kernel COW.
        auto const mfd{ ::memfd_create( "psi_vm_cow", MFD_CLOEXEC ) };
        if ( mfd != -1 )
        {
            if ( ::ftruncate( mfd, static_cast<off_t>( total_mapped ) ) != 0 ) [[ unlikely ]]
            {
                BOOST_ASSERT( errno == ENOMEM || errno == ENOSPC );
                ::close( mfd );
                detail::throw_bad_alloc();
            }

            // Map the memfd as MAP_SHARED to write the source data
            auto * const tmp_map{ ::mmap( nullptr, total_mapped, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0 ) };
            if ( tmp_map == MAP_FAILED ) [[ unlikely ]]
            {
                BOOST_ASSERT( errno == ENOMEM );
                ::close( mfd );
                detail::throw_bad_alloc();
            }
            std::memcpy( tmp_map, source.view_.data(), total_mapped );
            BOOST_VERIFY( ::munmap( tmp_map, total_mapped ) == 0 );

            flags::viewing const cow_view_flags
            {
                .protection = PROT_READ | PROT_WRITE,
                .flags      = MAP_PRIVATE
            };
            mapping_ = { posix::handle{ mfd }, cow_view_flags, total_mapped };
            mapping_.set_ephemeral(); // memfd: fd-backed but not on-disk
            view_ = mapped_view::map( mapping_, cow_view_flags, 0, total_mapped );
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
    view_    = mapped_view::map( mapping_, cow_view_flags, 0, total_mapped );
    std::memcpy( view_.data(), source.view_.data(), total_mapped );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
