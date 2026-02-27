////////////////////////////////////////////////////////////////////////////////
///
/// \file dirty_tracker.hpp
/// -----------------------
///
/// Non-polymorphic dirty page tracking for COW commit.
///
/// Single struct with platform-specific internals:
///   - Linux: userfaultfd WP_ASYNC (kernel 6.1+) or soft-dirty via pagemap
///   - Windows: NtQueryVirtualMemory(MemoryWorkingSetExInformation)
///   - macOS: no kernel tracking (MINCORE_MODIFIED tracks pager-level modification,
///            not COW-since-clone — empirically validated 2026-02-27)
///   - Fallback: node-level dirty flag (always available, all platforms)
///
/// The tracker is a value type -- no virtual dispatch, no heap allocation.
/// One tracker per COW clone (each clone has its own mapping range).
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm::detail
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// \class dirty_tracker
///
/// Tracks which pages in a memory range have been written since the last
/// snapshot. Usage pattern:
///
///   tracker.arm( base, size );      // after COW clone
///   // ... mutations happen ...
///   tracker.snapshot();              // batch-query all pages
///   if ( tracker.is_dirty( off ) )   // per-node check in commit loop
///       memcpy( ... );
///   tracker.clear();                 // re-arm for next cycle
///
/// On platforms without kernel-level tracking, snapshot() is a no-op and
/// is_dirty() always returns true (commit falls back to memcmp per node).
////////////////////////////////////////////////////////////////////////////////

struct dirty_tracker
{
    dirty_tracker() noexcept;
    ~dirty_tracker() noexcept;

    dirty_tracker( dirty_tracker const & ) = delete;
    dirty_tracker & operator=( dirty_tracker const & ) = delete;
    dirty_tracker( dirty_tracker && ) noexcept;
    dirty_tracker & operator=( dirty_tracker && ) noexcept;

    /// Arm tracking on the given address range. Called after COW clone.
    void arm( std::byte * address, std::size_t size ) noexcept;

    /// Batch-query the kernel for dirty state of all pages.
    /// Call once before the commit loop. Lazy: caches results internally.
    void snapshot() noexcept;

    /// Check if the page at the given byte offset (from armed base) is dirty.
    /// Must call snapshot() first. offset must be page-aligned.
    [[ nodiscard ]] bool is_dirty( std::size_t page_offset ) const noexcept;

    /// Re-arm tracking after commit (clear dirty bits for next cycle).
    void clear() noexcept;

    /// True if this tracker has kernel-level page tracking (not just fallback).
    [[ nodiscard ]] bool has_kernel_tracking() const noexcept;

#ifdef __linux__
    // Detection mode exposed for the static detect_mode() helper.
    enum class mode : std::uint8_t { none, soft_dirty, userfaultfd };
#endif

private:
    std::byte * base_{};
    std::size_t size_{};
    bool        snapshotted_{ false };

#ifdef __linux__
    mode mode_{ mode::none };
    int  uffd_fd_{ -1 };
    int  pm_fd_  { -1 };
    // Pagemap entries cached by snapshot(), one uint64_t per page.
    std::uint64_t * pagemap_cache_{};
    std::size_t     num_pages_{};
#elif defined( _WIN32 )
    // NtQueryVirtualMemory results cached by snapshot().
    // Opaque pointer to avoid pulling in Windows headers.
    void *      ws_info_{};
    std::size_t num_pages_{};
#elif defined( __APPLE__ )
    // No platform data: macOS has no usable kernel-level COW dirty tracking.
    // All methods are trivial no-ops (is_dirty always returns true).
#endif
}; // struct dirty_tracker

//------------------------------------------------------------------------------
} // namespace psi::vm::detail
//------------------------------------------------------------------------------
