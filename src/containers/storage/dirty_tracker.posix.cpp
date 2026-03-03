////////////////////////////////////////////////////////////////////////////////
///
/// \file dirty_tracker.posix.cpp
/// -----------------------------
///
/// Linux-specific dirty page tracking implementations:
///   Layer 1: userfaultfd UFFD_FEATURE_WP_ASYNC (kernel 6.1+)
///   Layer 2: soft-dirty via /proc/self/pagemap (kernel 3.11+)
///
/// /proc/self/pagemap format (per-page, 64-bit entries):
///   Bit 55: soft-dirty (set by kernel on write after clear_refs "4")
///   Bit 57: uffd-wp (SET = write-protected/clean, CLEARED = written/dirty)
///   Read via pread() at file offset = vpn * 8 (vpn = vaddr / page_size)
///
/// clear_refs semantics:
///   Writing "4" to /proc/self/clear_refs clears soft-dirty bits on ALL PTEs
///   process-wide. This is a limitation -- it affects unrelated mappings too.
///   userfaultfd WP_ASYNC is per-range and doesn't have this problem.
///
/// userfaultfd WP_ASYNC (bit 57 inversion):
///   After UFFDIO_WRITEPROTECT with UFFDIO_WRITEPROTECT_MODE_WP, pages have
///   the uffd-wp bit SET in pagemap (= protected = clean). When a write
///   occurs, the kernel auto-resolves the WP fault (WP_ASYNC = no signal
///   delivered) and CLEARS the uffd-wp bit. So: bit 57 SET -> clean,
///   bit 57 CLEARED -> dirty. This is inverted compared to soft-dirty bit 55.
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "dirty_tracker.hpp"

#ifdef __linux__

#include <psi/vm/allocation.hpp> // page_size
#include <psi/vm/allocators/allocator_base.hpp> // detail::throw_bad_alloc

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/userfaultfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib> // malloc/free
#include <cstring> // memset
//------------------------------------------------------------------------------
namespace psi::vm::detail
{
//------------------------------------------------------------------------------

namespace {

    int userfaultfd_create() noexcept
    {
        return static_cast<int>( ::syscall( SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK ) );
    }

    bool userfaultfd_wp_async_available() noexcept
    {
        auto const uffd{ userfaultfd_create() };
        if ( uffd == -1 )
            return false;

        struct uffdio_api api_req{};
        api_req.api      = UFFD_API;
        api_req.features = UFFD_FEATURE_WP_ASYNC;

        auto const ok{ ::ioctl( uffd, UFFDIO_API, &api_req ) == 0 };
        ::close( uffd );

        return ok && ( api_req.features & UFFD_FEATURE_WP_ASYNC );
    }

    static bool soft_dirty_available() noexcept
    {
        auto const fd{ ::open( "/proc/self/pagemap", O_RDONLY ) };
        if ( fd == -1 )
            return false;
        ::close( fd );
        return true;
    }

    // Detect best available mode (called once, cached)
    dirty_tracker::mode detect_mode() noexcept
    {
        using mode = dirty_tracker::mode;
        // Prefer userfaultfd (per-range, no process-wide side effects)
        if ( userfaultfd_wp_async_available() )
            return mode::userfaultfd;
        // Fall back to soft-dirty (process-wide clear_refs)
        if ( soft_dirty_available() )
            return mode::soft_dirty;
        // No kernel tracking available
        return mode::none;
    }

    dirty_tracker::mode best_mode() noexcept
    {
        static auto const m{ detect_mode() };
        return m;
    }
} // anonymous namespace


//--- Construction / destruction -----------------------------------------------

dirty_tracker::dirty_tracker() noexcept
    : mode_{ best_mode() }
{}

dirty_tracker::~dirty_tracker() noexcept
{
    std::free( pagemap_cache_ );
    if ( uffd_fd_ != -1 ) ::close( uffd_fd_ );
    if ( pm_fd_   != -1 ) ::close( pm_fd_   );
}

dirty_tracker::dirty_tracker( dirty_tracker && other ) noexcept
    : base_         { other.base_          }
    , size_         { other.size_          }
    , snapshotted_  { other.snapshotted_   }
    , mode_         { other.mode_          }
    , uffd_fd_      { other.uffd_fd_       }
    , pm_fd_        { other.pm_fd_         }
    , pagemap_cache_{ other.pagemap_cache_ }
    , num_pages_    { other.num_pages_     }
{
    other.base_          = nullptr;
    other.size_          = 0;
    other.snapshotted_   = false;
    other.uffd_fd_       = -1;
    other.pm_fd_         = -1;
    other.pagemap_cache_ = nullptr;
    other.num_pages_     = 0;
}

dirty_tracker & dirty_tracker::operator=( dirty_tracker && other ) noexcept
{
    if ( this != &other )
    {
        std::free( pagemap_cache_ );
        if ( uffd_fd_ != -1 ) ::close( uffd_fd_ );
        if ( pm_fd_   != -1 ) ::close( pm_fd_   );

        base_          = other.base_;
        size_          = other.size_;
        snapshotted_   = other.snapshotted_;
        mode_          = other.mode_;
        uffd_fd_       = other.uffd_fd_;
        pm_fd_         = other.pm_fd_;
        pagemap_cache_ = other.pagemap_cache_;
        num_pages_     = other.num_pages_;

        other.base_          = nullptr;
        other.size_          = 0;
        other.snapshotted_   = false;
        other.uffd_fd_       = -1;
        other.pm_fd_         = -1;
        other.pagemap_cache_ = nullptr;
        other.num_pages_     = 0;
    }
    return *this;
}


//--- arm ----------------------------------------------------------------------

void dirty_tracker::arm( std::byte * const address, std::size_t const size )
{
    base_        = address;
    size_        = size;
    snapshotted_ = false;
    auto const required_pages{ ( size + page_size - 1 ) / page_size };

    // Ensure pagemap cache is large enough
    auto * const new_cache{ static_cast<std::uint64_t *>(
        std::realloc( pagemap_cache_, required_pages * sizeof( std::uint64_t ) )
    ) };
    if ( required_pages && !new_cache )
        throw_bad_alloc();

    pagemap_cache_ = new_cache;
    num_pages_     = required_pages;

    // Open pagemap fd (cached)
    if ( pm_fd_ == -1 )
        pm_fd_ = ::open( "/proc/self/pagemap", O_RDONLY );

    if ( mode_ == mode::userfaultfd )
    {
        // Create userfaultfd if needed
        if ( uffd_fd_ == -1 )
        {
            uffd_fd_ = userfaultfd_create();
            if ( uffd_fd_ != -1 )
            {
                struct uffdio_api api_req{};
                api_req.api      = UFFD_API;
                api_req.features = UFFD_FEATURE_WP_ASYNC;
                if ( ::ioctl( uffd_fd_, UFFDIO_API, &api_req ) != 0 )
                {
                    ::close( uffd_fd_ );
                    uffd_fd_ = -1;
                    mode_    = mode::soft_dirty; // downgrade
                }
            }
            else
            {
                mode_ = mode::soft_dirty; // downgrade
            }
        }

        if ( mode_ == mode::userfaultfd )
        {
            // Register range for WP monitoring
            struct uffdio_register reg{};
            reg.range.start = reinterpret_cast<std::uintptr_t>( address );
            reg.range.len   = size;
            reg.mode        = UFFDIO_REGISTER_MODE_WP;
            ::ioctl( uffd_fd_, UFFDIO_REGISTER, &reg );

            // Write-protect all pages
            struct uffdio_writeprotect wp{};
            wp.range.start = reinterpret_cast<std::uintptr_t>( address );
            wp.range.len   = size;
            wp.mode        = UFFDIO_WRITEPROTECT_MODE_WP;
            ::ioctl( uffd_fd_, UFFDIO_WRITEPROTECT, &wp );
            return;
        }
    }

    if ( mode_ == mode::soft_dirty )
    {
        // Clear all soft-dirty bits process-wide
        auto const cr_fd{ ::open( "/proc/self/clear_refs", O_WRONLY ) };
        if ( cr_fd != -1 )
        {
            [[maybe_unused]] auto const written{ ::write( cr_fd, "4\n", 2 ) };
            ::close( cr_fd );
        }
    }
}


//--- snapshot -----------------------------------------------------------------

void dirty_tracker::snapshot() noexcept
{
    if ( snapshotted_ || mode_ == mode::none || pm_fd_ == -1 || !pagemap_cache_ )
        return;

    // Batch-read pagemap entries for all pages in the range.
    // Each entry is 8 bytes at file offset = vpn * 8.
    auto const base_vpn{ reinterpret_cast<std::uintptr_t>( base_ ) / page_size };
    auto const file_offset{ static_cast<off_t>( base_vpn * sizeof( std::uint64_t ) ) };
    auto const bytes_needed{ num_pages_ * sizeof( std::uint64_t ) };

    auto const bytes_read{ ::pread( pm_fd_, pagemap_cache_, bytes_needed, file_offset ) };
    if ( static_cast<std::size_t>( bytes_read ) != bytes_needed )
    {
        // Partial or failed read -- mark all as dirty (conservative)
        std::memset( pagemap_cache_, 0xFF, bytes_needed );
    }

    snapshotted_ = true;
}


//--- is_dirty -----------------------------------------------------------------

bool dirty_tracker::is_dirty( std::size_t const page_offset ) const noexcept
{
    if ( mode_ == mode::none || !snapshotted_ || !pagemap_cache_ )
        return true; // no tracking -- assume dirty

    auto const page_index{ page_offset / page_size };
    if ( page_index >= num_pages_ )
        return true;

    auto const entry{ pagemap_cache_[ page_index ] };

    if ( mode_ == mode::userfaultfd )
    {
        // Bit 57: uffd-wp. SET = write-protected (clean), CLEARED = dirty.
        return !( ( entry >> 57 ) & 1 );
    }
    else // mode::soft_dirty
    {
        // Bit 55: soft-dirty. SET = dirty.
        return ( entry >> 55 ) & 1;
    }
}


//--- clear --------------------------------------------------------------------

void dirty_tracker::clear() noexcept
{
    snapshotted_ = false;

    if ( mode_ == mode::userfaultfd && uffd_fd_ != -1 )
    {
        // Re-arm write-protection on the entire range
        struct uffdio_writeprotect wp{};
        wp.range.start = reinterpret_cast<std::uintptr_t>( base_ );
        wp.range.len   = size_;
        wp.mode        = UFFDIO_WRITEPROTECT_MODE_WP;
        ::ioctl( uffd_fd_, UFFDIO_WRITEPROTECT, &wp );
    }
    else if ( mode_ == mode::soft_dirty )
    {
        // Re-clear soft-dirty bits
        auto const cr_fd{ ::open( "/proc/self/clear_refs", O_WRONLY ) };
        if ( cr_fd != -1 )
        {
            [[maybe_unused]] auto const written{ ::write( cr_fd, "4\n", 2 ) };
            ::close( cr_fd );
        }
    }
}


//--- has_kernel_tracking ------------------------------------------------------

bool dirty_tracker::has_kernel_tracking() const noexcept
{
    return mode_ != mode::none;
}

//------------------------------------------------------------------------------
} // namespace psi::vm::detail
//------------------------------------------------------------------------------
#endif // __linux__
