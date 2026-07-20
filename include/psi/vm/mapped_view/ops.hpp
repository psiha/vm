////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2010 - 2024.
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

#include <psi/vm/error/error.hpp>
#include <psi/vm/mappable_objects/file/handle.hpp>
#include <psi/vm/span.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

#ifndef NDEBUG
// Test-only observation seam: invoked at entry to every flush_blocking(range,
// source_file) overload so a test can record which underlying file/range a
// given flush call actually targets (e.g. to verify a header flush hits the
// file it is documented to hit).
inline void (*flush_observer)( mapped_span range, file_handle::const_reference source_file ) noexcept = nullptr;

// Test-only fault-injection seam: when set, every flush_blocking(range,
// source_file) call fails (returns an error) without attempting the real
// OS call, exercising the exact same failure-propagation path a genuine
// ENOSPC/EIO would take. Deliberately not thread_local: tests using this are
// expected to be single-threaded for the duration they set it.
inline bool flush_force_failure = false;
#endif

// Why this can fail at all (it is not a purely theoretical concern):
//
// A range handed to flush_blocking() can, in full generality, cover pages
// that were never actually backed by storage before this call. Extending a
// file (ftruncate()/SetFileSize()) does not reserve real blocks for the new
// range on any mainstream filesystem (ext4, XFS, btrfs, NTFS, APFS): it
// creates a sparse "hole" (but see the note on extension cost below - not
// reserving blocks does not make the call unconditionally cheap). mmap()-ing that
// range, and writing into the mapped memory, don't touch storage either - a
// write into mapped memory is a plain CPU store that dirties a page-cache
// page; there is no disk-space check anywhere in that path. Real block
// allocation for a hole is deferred to *writeback* time (delayed allocation:
// ext4 "delalloc", similar mechanisms elsewhere) - which is exactly what
// flush_blocking() triggers. So a full or failing device is invisible all
// the way up to the point where this specific call tries to allocate the
// blocks it needs, and only becomes visible here, as this call's result.
//
// Why a single successful call afterwards does not retroactively prove
// safety (the 2018 PostgreSQL "fsyncgate" class of issue): background
// (non-explicit) writeback triggered by the kernel independently of any
// caller (e.g. dirty-page-ratio thresholds) can itself hit ENOSPC/EIO for a
// page nobody is synchronously waiting on. Historically (pre-4.13 Linux)
// that error was reported to only the first fsync-family call that happened
// to check, then discarded - a later flush_blocking() on the same file could
// return success even though earlier data was never durably written. Modern
// kernels (errseq_t-based reporting) narrow this to "since this file
// descriptor was opened or last checked", which is better but does not
// change the shape of the problem: once you know an error can be silently
// dropped, a clean return later is evidence about *later* writes, not proof
// that *earlier* ones landed. This is why callers that care about durability
// must check (or, per fallible_result below, at least not blindly discard)
// every single flush_blocking() call, not just periodically sample one.
//
// Note on extension cost: not reserving blocks is a statement about
// allocation, not about latency. Growing a file is *not* reliably a
// metadata-only operation. On XFS specifically it synchronously writes back
// and waits for the tail block when two conditions hold together:
//   1. the file currently has dirty (e.g. mmap-written) pages, and
//   2. its current end-of-file is not filesystem-block aligned.
// The size change is logged, so any data between the on-disk EOF and the new
// EOF - including the zeroing of the partial tail block - has to reach the
// device first, or a crash could expose unzeroed blocks past the old EOF
// (the "null files" problem). The path is
// xfs_setattr_size() -> filemap_write_and_wait_range() ->
// folio_wait_writeback() -> io_schedule(); i.e. the calling thread sleeps on
// a real device round trip, which on network-attached storage is expensive.
//
// Either condition alone is free - it is only the combination that pays.
// Measured over 2000 extending truncates, us/call, XFS on network-attached
// block storage, versus a tmpfs control:
//   clean file, any alignment .....   0.7
//   dirty pages, aligned EOF ......   1.1
//   dirty pages, unaligned EOF .... 644.8
//   tmpfs (control) ...............   0.2
// A downstream workload that grows mapped files incrementally found the
// overwhelming majority of its file-growth time in this one wait. Callers
// who grow mapped files in a loop therefore want either to keep the file's
// EOF block-aligned or to grow in fewer, larger steps; note also that the
// kernel holds i_rwsem exclusively across that wait, so concurrent buffered
// readers of the same file serialise behind it (mmap readers, which fault
// without taking i_rwsem, do not).
//
// This is why flush_blocking() returns a psi.err fallible_result rather than
// a bool: a bool can be silently ignored with zero cost, which is exactly
// the wrong default for an operation whose failure this consequential.
// fallible_result<void> still allows an explicit ignore (.ignore_failure(),
// or checking it and branching) for callers that have a genuine
// fire-and-forget use case, but a bare discarded return - the "I didn't even
// think about it" case - throws instead of vanishing.
// Deliberately not [[nodiscard]] - see set_size()/vm_vector.cpp for the same
// convention: a bare, discarded call is the normal "throw on error, otherwise
// proceed" usage (fallible_result's destructor enforces this), not a mistake
// to warn about.
fallible_result<void> flush_blocking( mapped_span range, file_handle::const_reference source_file ) noexcept;
#ifndef _WIN32
// No way to perform this on Windows through only the view itself
// https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-flushviewoffile
fallible_result<void> flush_blocking( mapped_span range ) noexcept;
#endif
void flush_async   ( mapped_span range ) noexcept;

// these below ought to go/get special versions in allocation.hpp
void discard( mapped_span range ) noexcept;

// TODO prefetch/WILL_NEED
// https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-prefetchvirtualmemory


#ifndef _WIN32
// utility verbosity reducing wrapper
void madvise( mapped_span range, int advice ) noexcept;
#endif

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
