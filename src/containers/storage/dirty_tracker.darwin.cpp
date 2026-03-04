////////////////////////////////////////////////////////////////////////////////
///
/// \file dirty_tracker.darwin.cpp
/// ------------------------------
///
/// macOS dirty page tracking — no-op stub.
/// (no kernel tracking APIs available, mprotect() + SIGSEGV trick is the only
/// option - too slow and complex to be worth implementing)
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifdef __APPLE__
#include "dirty_tracker.hpp"
//------------------------------------------------------------------------------
namespace psi::vm::detail
{
//------------------------------------------------------------------------------

dirty_tracker:: dirty_tracker()                   noexcept = default;
dirty_tracker::~dirty_tracker()                   noexcept = default;
dirty_tracker:: dirty_tracker( dirty_tracker && ) noexcept = default;
dirty_tracker & dirty_tracker::operator=( dirty_tracker && ) noexcept = default;

void dirty_tracker::arm     ( std::byte *, std::size_t ) {}
void dirty_tracker::snapshot() noexcept {}
void dirty_tracker::clear   () noexcept {}

bool dirty_tracker::is_dirty( std::size_t ) const noexcept { return true ; } // always dirty → caller uses node dirty flag
bool dirty_tracker::has_kernel_tracking()   const noexcept { return false; }

//------------------------------------------------------------------------------
} // namespace psi::vm::detail
//------------------------------------------------------------------------------
#endif // __APPLE__
