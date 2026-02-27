////////////////////////////////////////////////////////////////////////////////
///
/// \file dirty_tracker.darwin.cpp
/// ------------------------------
///
/// macOS dirty page tracking — no-op stub.
///
/// EMPIRICALLY VALIDATED (2026-02-27 on macOS 26.3 / Apple M1):
///   MINCORE_MODIFIED does NOT track COW dirty pages. All pages report
///   modified=1 immediately after mach_vm_remap(copy=TRUE), before any
///   writes. The bit tracks "ever modified since page allocation" (pager
///   perspective), not "modified since COW clone".
///
///   Conclusion: MINCORE_MODIFIED is useless for COW dirty tracking on macOS.
///   has_kernel_tracking() returns false. commit_to() falls back to the
///   node-level dirty flag (which works on all platforms).
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "dirty_tracker.hpp"

#ifdef __APPLE__

//------------------------------------------------------------------------------
namespace psi::vm::detail
{
//------------------------------------------------------------------------------

dirty_tracker:: dirty_tracker()                          noexcept = default;
dirty_tracker::~dirty_tracker()                          noexcept = default;
dirty_tracker:: dirty_tracker( dirty_tracker && )        noexcept = default;
dirty_tracker & dirty_tracker::operator=( dirty_tracker && other ) noexcept = default;

void dirty_tracker::arm     ( std::byte *, std::size_t ) noexcept {}
void dirty_tracker::snapshot()                           noexcept {}
void dirty_tracker::clear   ()                           noexcept {}

bool dirty_tracker::is_dirty( std::size_t ) const noexcept { return true; } // always dirty → caller uses node dirty flag
bool dirty_tracker::has_kernel_tracking()   const noexcept { return false; }

//------------------------------------------------------------------------------
} // namespace psi::vm::detail

#endif // __APPLE__
//------------------------------------------------------------------------------
