////////////////////////////////////////////////////////////////////////////////
///
/// \file populate.hpp
/// ------------------
///
/// Pre-faulting of memory: populate() makes the pages of a range resident now,
/// in one call, instead of one page fault on each first touch.
///
/// A fresh anonymous page costs a fault (about a quarter of a microsecond of
/// kernel time and more under virtualisation) on its first write. Memory that
/// is about to be written whole - the part of a container that a bulk
/// operation will fill, a sort's scratch buffer - can be faulted in by one
/// call that skips the per-page trap: 1 GiB took 152 ms populated against
/// 263 ms touched (WSL2, kernel 6.6, 4 KiB pages). It pays only where the
/// pages WILL be written (a populated page is resident, and counts toward the
/// footprint, whether it is used or not) and where huge pages do not already
/// make the fault per 2 MiB.
///
/// Where it is available: Linux (MADV_POPULATE_READ/WRITE, Linux 5.14; the
/// library assumes a 6.x kernel, so there is nothing to detect). Elsewhere
/// it does nothing and says so: Windows has no call that populates committed,
/// never touched memory (PrefetchVirtualMemory leaves it non-resident, which
/// was measured), and macOS has none for anonymous memory.
///
/// On memory backed by transparent huge pages the kernel populates the whole
/// huge page that a populated page lies in, so more than the range can
/// become resident.
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
/// (See accompanying file LICENSE_1_0.txt or copy at
/// http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

//! What the populated pages are made ready for: write makes them private and
//! writable at once (for a private mapping of a file this copies the file's
//! page, and for a shared file mapping it reads and dirties it); read only
//! maps them, a zero page for memory that was never written.
enum class populate_access : bool { read, write };

//! Whether populate() can do anything on this system (see the file comment).
[[ nodiscard ]] constexpr bool can_populate() noexcept
{
#if defined( __linux__ ) && !defined( __ANDROID__ )
    return true;
#else
    return false;
#endif
}

//! Faults in the whole pages that lie inside [ address, address + size ) - the
//! range is rounded inwards to page boundaries, so a neighbour's page is never
//! touched - and returns whether they all are populated: false where
//! can_populate() is false, or where the system refused part of the range (no
//! memory, a hole in the mapping, a mapping that cannot be populated; the
//! pages done before that stay populated),
//! true for a range that holds no whole page. It reports nothing else and
//! never throws: the pages fault in on first touch as they would have.
bool populate( void * address, std::size_t size, populate_access = populate_access::write ) noexcept;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
