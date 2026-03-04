////////////////////////////////////////////////////////////////////////////////
///
/// \file mach.hpp
/// --------------
///
/// Convenience wrappers for Mach VM APIs (macOS).
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once
#ifdef __APPLE__

#include <mach/mach_init.h>
#include <mach/mach_vm.h>
#include <mach/vm_statistics.h>
//------------------------------------------------------------------------------
namespace psi::vm::mach
{
//------------------------------------------------------------------------------

// mach_task_self() is a macro expanding to the cached mach_task_self_ global.
// This alias mirrors the nt::current_process pattern for consistency.
inline auto const current_task{ ::mach_task_self() };

/// Remap pages within the current task: replace the target region with
/// pages from the source region (same process).
///
/// Common parameters are fixed:
///   - target/source task = current_task (same process)
///   - alignment mask     = 0 (natural alignment)
///   - flags              = VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE (replace existing mapping)
///
/// \param target_addr  [in/out] target address (must point to an existing mapping)
/// \param size         number of bytes to remap
/// \param source_addr  source address to copy/share pages from
/// \param copy         TRUE = COW copy, FALSE = move/share (zero-copy)
/// \param inherit      VM_INHERIT_COPY or VM_INHERIT_NONE
/// \returns KERN_SUCCESS on success
[[ nodiscard ]]
inline kern_return_t vm_remap_overwrite(
    mach_vm_address_t * const target_addr,
    mach_vm_size_t      const size,
    void        const * const source_addr,
    boolean_t           const copy,
    vm_inherit_t        const inherit
) noexcept
{
    vm_prot_t cur_prot;
    vm_prot_t max_prot;
    return ::mach_vm_remap
    (
        current_task, target_addr, size,
        0,            // alignment mask
        VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
        current_task,
        reinterpret_cast<mach_vm_address_t>( source_addr ),
        copy,
        &cur_prot, &max_prot,
        inherit
    );
}

//------------------------------------------------------------------------------
} // namespace psi::vm::mach
//------------------------------------------------------------------------------
#endif // __APPLE__
