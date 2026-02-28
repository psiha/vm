////////////////////////////////////////////////////////////////////////////////
///
/// \file dirty_tracker.win32.cpp
/// -----------------------------
///
/// Windows dirty page tracking via NtQueryVirtualMemory with
/// MemoryWorkingSetExInformation.
///
/// Detection principle:
///   After a COW clone (PAGE_WRITECOPY mapping), clean pages remain shared
///   (Shared=1 in the working-set-ex info). When a page is written, the OS
///   performs the COW break: the page gets a private copy and transitions to
///   PAGE_READWRITE with Shared=0. We detect this transition.
///
/// Batch query:
///   A single NtQueryVirtualMemory call queries the entire mapped range.
///   Results are cached by snapshot() and indexed by page offset in is_dirty().
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "dirty_tracker.hpp"

#ifdef _WIN32

#include <psi/vm/allocation.hpp> // page_size
#include <psi/vm/detail/nt.hpp>  // NtQueryVirtualMemory, MEMORY_WORKING_SET_EX_INFORMATION

#include <cstdlib> // malloc/free
#include <cstring> // memset
//------------------------------------------------------------------------------
namespace psi::vm::detail
{
//------------------------------------------------------------------------------

using working_set_ex_info = nt::MEMORY_WORKING_SET_EX_INFORMATION;


//--- Construction / destruction -----------------------------------------------

dirty_tracker::dirty_tracker() noexcept = default;

dirty_tracker::~dirty_tracker() noexcept
{
    std::free( ws_info_ );
}

dirty_tracker::dirty_tracker( dirty_tracker && other ) noexcept
    : base_       { other.base_        }
    , size_       { other.size_        }
    , snapshotted_{ other.snapshotted_ }
    , ws_info_    { other.ws_info_     }
    , num_pages_  { other.num_pages_   }
{
    other.base_        = nullptr;
    other.size_        = 0;
    other.snapshotted_ = false;
    other.ws_info_     = nullptr;
    other.num_pages_   = 0;
}

dirty_tracker & dirty_tracker::operator=( dirty_tracker && other ) noexcept
{
    if ( this != &other )
    {
        std::free( ws_info_ );

        base_        = other.base_;
        size_        = other.size_;
        snapshotted_ = other.snapshotted_;
        ws_info_     = other.ws_info_;
        num_pages_   = other.num_pages_;

        other.base_        = nullptr;
        other.size_        = 0;
        other.snapshotted_ = false;
        other.ws_info_     = nullptr;
        other.num_pages_   = 0;
    }
    return *this;
}


//--- arm ----------------------------------------------------------------------

void dirty_tracker::arm( std::byte * const address, std::size_t const size ) noexcept
{
    base_        = address;
    size_        = size;
    snapshotted_ = false;
    num_pages_   = ( size + ::psi::vm::page_size - 1 ) / ::psi::vm::page_size;

    // Ensure ws_info_ buffer is large enough
    auto * const new_buf{ std::realloc( ws_info_, num_pages_ * sizeof( working_set_ex_info ) ) };
    if ( new_buf )
        ws_info_ = new_buf;
}


//--- snapshot -----------------------------------------------------------------

void dirty_tracker::snapshot() noexcept
{
    if ( snapshotted_ || !ws_info_ || !num_pages_ )
        return;

    auto * const info{ static_cast<working_set_ex_info *>( ws_info_ ) };

    // Fill VirtualAddress for each page
    for ( std::size_t i{ 0 }; i < num_pages_; ++i )
    {
        info[ i ].VirtualAddress = base_ + i * ::psi::vm::page_size;
    }

    // Single NT API call to query all pages
    auto const status{ nt::NtQueryVirtualMemory(
        nt::current_process,
        nullptr,
        nt::MemoryWorkingSetExInformation,
        info,
        num_pages_ * sizeof( working_set_ex_info ),
        nullptr
    ) };

    if ( status < 0 ) [[ unlikely ]] // NTSTATUS: negative = error
    {
        // Query failed -- mark all as dirty (conservative)
        for ( std::size_t i{ 0 }; i < num_pages_; ++i )
        {
            info[ i ].VirtualAttributes.Valid  = 0;
            info[ i ].VirtualAttributes.Shared = 0;
        }
    }

    snapshotted_ = true;
}


//--- is_dirty -----------------------------------------------------------------

bool dirty_tracker::is_dirty( std::size_t const page_offset ) const noexcept
{
    if ( !snapshotted_ || !ws_info_ )
        return true;

    auto const page_index{ page_offset / ::psi::vm::page_size };
    if ( page_index >= num_pages_ )
        return true;

    auto const & info{ static_cast<working_set_ex_info const *>( ws_info_ )[ page_index ] };

    // A page is dirty (COW-broken) if:
    //   - Valid=1 and Shared=0 (private copy = written)
    //   - Valid=0 (not in working set -- might have been written and paged out)
    // Conservative: treat !Valid as dirty.
    if ( !info.VirtualAttributes.Valid )
        return true;

    // Shared=1 means the page is still shared with the COW source (clean).
    // Shared=0 means the OS performed a COW break (dirty).
    return !info.VirtualAttributes.Shared;
}


//--- clear --------------------------------------------------------------------

void dirty_tracker::clear() noexcept
{
    snapshotted_ = false;
    // No re-arming needed on Windows -- the COW protection is maintained by
    // the OS. Once a page is COW-broken it stays private. The next commit
    // cycle will re-snapshot and detect newly broken pages.
}


//--- has_kernel_tracking ------------------------------------------------------

bool dirty_tracker::has_kernel_tracking() const noexcept
{
    // NtQueryVirtualMemory is always available on Windows NT.
    return nt::NtQueryVirtualMemory != nullptr;
}

//------------------------------------------------------------------------------
} // namespace psi::vm::detail

#endif // _WIN32
//------------------------------------------------------------------------------
