////////////////////////////////////////////////////////////////////////////////
///
/// \file expand_darwin.hpp
/// -----------------------
///
/// Shared macOS expand primitive: mach_vm_allocate + mach_vm_remap.
///
/// Allocates a new region of the target size, then remaps the old pages
/// (zero-copy, copy=FALSE) into the beginning of the new region.
/// The old region is deallocated on success.
///
/// Copyright (c) Domagoj Saric 2023 - 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once
#ifdef __APPLE__

#include "../detail/mach.hpp"

#include <cstddef>
#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm::detail
{
//------------------------------------------------------------------------------

struct mach_expand_result
{
    void      * address{ nullptr };
    std::size_t size   { 0 };
    explicit operator bool() const noexcept { return address != nullptr; }
};

/// macOS expand via mach_vm_remap: allocate new region + zero-copy page remap.
/// On success, the old region is deallocated and the new (larger) region
/// contains the old pages at its beginning.  Preserves COW page state.
///
/// Returns nullptr on failure (caller should fall back to memcpy).
[[ nodiscard, gnu::cold ]]
inline mach_expand_result macos_expand_relocate(
    void        * const address,
    std::size_t   const current_size,
    std::size_t   const target_size
) noexcept
{
    mach_vm_address_t new_addr{ 0 };
    auto kr{ ::mach_vm_allocate( mach::current_task, &new_addr, target_size, VM_FLAGS_ANYWHERE ) };
    if ( kr != KERN_SUCCESS ) [[ unlikely ]]
        return {};

    kr = mach::vm_remap_overwrite
    (
        &new_addr,
        current_size,
        address,
        FALSE, // copy=FALSE: move/share pages (zero-copy)
        VM_INHERIT_NONE
    );
    if ( kr != KERN_SUCCESS ) [[ unlikely ]]
    {
        // remap failed: free the speculative allocation
        ::mach_vm_deallocate( mach::current_task, new_addr, target_size );
        return {};
    }

    ::mach_vm_deallocate( mach::current_task, reinterpret_cast<mach_vm_address_t>( address ), current_size );
    return { reinterpret_cast<void *>( new_addr ), target_size };
}

//------------------------------------------------------------------------------
} // namespace psi::vm::detail
//------------------------------------------------------------------------------
#endif // __APPLE__
