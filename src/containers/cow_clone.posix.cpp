////////////////////////////////////////////////////////////////////////////////
///
/// \file cow_clone.posix.cpp
/// -------------------------
///
/// POSIX COW clone implementation:
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
#include "cow_clone_impl.hpp"

#include <psi/vm/handles/handle.hpp>

#ifdef __APPLE__
#   include <mach/mach_init.h>
#   include <mach/mach_vm.h>
#   include <mach/vm_statistics.h>
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

#include <cstring> // memcpy
//------------------------------------------------------------------------------
namespace psi::vm::detail
{
//------------------------------------------------------------------------------

namespace
{
    // fd-backed COW: dup the fd + MAP_PRIVATE view.
    // Works identically for real files and memfds on Linux and macOS.
    [[ gnu::cold ]]
    cow_clone_result cow_clone_file_backed( mapping const & src_mapping, std::size_t const total_mapped )
    {
        auto const cow_fd{ posix::handle_traits::copy( src_mapping.get() ) };
        if ( cow_fd == posix::handle_traits::invalid_value ) [[ unlikely ]]
            return {};

        flags::viewing cow_view_flags
        {
            .protection = PROT_READ | PROT_WRITE,
            .flags      = MAP_PRIVATE
        };
        mapping cow_mapping{ posix::handle{ cow_fd }, cow_view_flags, total_mapped };
        if ( !src_mapping.is_file_based() ) // source is ephemeral (memfd) → clone is too
            cow_mapping.set_ephemeral();

        auto cow_view{ mapped_view::map( cow_mapping, cow_view_flags, 0, total_mapped ).as_result_or_error() };
        if ( !cow_view ) [[ unlikely ]]
            return {};

        return { std::move( cow_mapping ), *std::move( cow_view ) };
    }

#ifdef __APPLE__
    // macOS anonymous: mach_vm_remap(copy=TRUE) for zero-copy COW.
    // Allocates a target anonymous mapping, then replaces its pages with
    // COW copies of the source pages.
    [[ gnu::cold ]]
    cow_clone_result cow_clone_anon_mach
    (
        mapped_span const src_view,
        std::size_t const total_mapped
    )
    {
        // Create a proper anonymous mapping via the standard path
        flags::viewing cow_view_flags
        {
            .protection = PROT_READ | PROT_WRITE,
            .flags      = MAP_PRIVATE | MAP_ANONYMOUS
        };
        mapping cow_mapping{ posix::handle{}, cow_view_flags, total_mapped };

        auto cow_view{ mapped_view::map( cow_mapping, cow_view_flags, 0, total_mapped ).as_result_or_error() };
        if ( !cow_view ) [[ unlikely ]]
            return {};

        // Replace the anonymous pages with COW copies of the source pages
        auto       target_addr{ reinterpret_cast<mach_vm_address_t>( cow_view->data() ) };
        auto const source_addr{ reinterpret_cast<mach_vm_address_t>( const_cast<std::byte *>( src_view.data() ) ) };
        vm_prot_t cur_prot, max_prot;
        auto const kr
        {
            ::mach_vm_remap
            (
                ::mach_task_self(),
                &target_addr,
                total_mapped,
                0,
                VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
                ::mach_task_self(),
                source_addr,
                TRUE, // copy = TRUE -> COW
                &cur_prot,
                &max_prot,
                VM_INHERIT_COPY
            )
        };
        if ( kr != KERN_SUCCESS ) [[ unlikely ]]
            return {}; // cow_view destructor unmaps the anonymous region

        return { std::move( cow_mapping ), *std::move( cow_view ) };
    }
#endif // __APPLE__

#ifdef __linux__
    // Linux anonymous: memfd_create + memcpy + MAP_PRIVATE for true kernel COW.
    // The initial memcpy populates the memfd; subsequent writes to the clone
    // are page-granular COW (only modified pages get private copies).
    [[ gnu::cold ]]
    cow_clone_result cow_clone_anon_memfd
    (
        mapped_span const src_view,
        std::size_t const total_mapped
    )
    {
        auto const mfd{ ::memfd_create( "psi_vm_cow", MFD_CLOEXEC ) };
        if ( mfd == -1 ) [[ unlikely ]]
            return {};

        if ( ::ftruncate( mfd, static_cast<off_t>( total_mapped ) ) != 0 ) [[ unlikely ]]
        {
            ::close( mfd );
            return {};
        }

        // Map the memfd as MAP_SHARED to write the source data
        auto * const tmp_map{ ::mmap( nullptr, total_mapped, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0 ) };
        if ( tmp_map == MAP_FAILED ) [[ unlikely ]]
        {
            ::close( mfd );
            return {};
        }
        std::memcpy( tmp_map, src_view.data(), total_mapped );
        ::munmap( tmp_map, total_mapped );

        // Create the COW clone as MAP_PRIVATE of the memfd
        flags::viewing cow_view_flags
        {
            .protection = PROT_READ | PROT_WRITE,
            .flags      = MAP_PRIVATE
        };
        mapping cow_mapping{ posix::handle{ mfd }, cow_view_flags, total_mapped };

        auto cow_view{ mapped_view::map( cow_mapping, cow_view_flags, 0, total_mapped ).as_result_or_error() };
        if ( !cow_view ) [[ unlikely ]]
            return {};

        return { std::move( cow_mapping ), *std::move( cow_view ) };
    }
#endif // __linux__

} // anonymous namespace


[[ gnu::cold ]]
cow_clone_result
cow_clone
(
    mapping     const & src_mapping,
    mapped_span const   src_view,
    std::size_t const   total_mapped
)
{
    // fd-backed (real file or memfd): dup + MAP_PRIVATE gives kernel COW.
    if ( src_mapping.has_fd() )
        return cow_clone_file_backed( src_mapping, total_mapped );

    // Anonymous (no fd): platform-specific
#if defined( __APPLE__ )
    {
        auto result{ cow_clone_anon_mach( src_view, total_mapped ) };
        if ( result )
            return result;
    }
    // mach_vm_remap failed: fall through to memcpy
#elif defined( __linux__ )
    {
        auto result{ cow_clone_anon_memfd( src_view, total_mapped ) };
        if ( result )
        {
            result.mapping_.set_ephemeral(); // memfd: fd-backed but not on-disk
            return result;
        }
    }
    // memfd_create failed: fall through to memcpy
#endif

    // Universal fallback: allocate anonymous mapping + deep copy
    flags::viewing cow_view_flags
    {
        .protection = PROT_READ | PROT_WRITE,
        .flags      = MAP_PRIVATE | MAP_ANONYMOUS
    };
    mapping cow_mapping{ posix::handle{}, cow_view_flags, total_mapped };

    auto cow_view{ mapped_view::map( cow_mapping, cow_view_flags, 0, total_mapped ).as_result_or_error() };
    if ( !cow_view ) [[ unlikely ]]
        return {};

    std::memcpy( cow_view->data(), src_view.data(), total_mapped );

    return { std::move( cow_mapping ), *std::move( cow_view ) };
}

//------------------------------------------------------------------------------
} // namespace psi::vm::detail
//------------------------------------------------------------------------------
