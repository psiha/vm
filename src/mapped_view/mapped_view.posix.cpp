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
#include <psi/vm/align.hpp>
#include <psi/vm/detail/posix.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>
#include <psi/vm/mapped_view/ops.hpp>

#include <boost/assert.hpp>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace posix
{
//------------------------------------------------------------------------------

// OSX syscalls https://github.com/opensource-apple/xnu/blob/master/bsd/kern/syscalls.master

namespace
{
#if !defined( MAP_ALIGNED_SUPER )
    auto constexpr MAP_ALIGNED_SUPER{ 0 }; // FreeBSD specific hint for large pages https://man.freebsd.org/cgi/man.cgi?sektion=2&query=mmap
#endif // MAP_ALIGNED_SUPER
} // anonymous namespace

BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
void * mmap( void * const target_address, std::size_t const size, int const protection, int const flags, int const file_handle, std::uint64_t const offset ) noexcept
{
    BOOST_ASSUME( is_aligned( target_address, reserve_granularity )                      );
  //BOOST_ASSUME( is_aligned( size          , reserve_granularity ) || file_handle != -1 ); // Linux allows seems to allow nonaligned size even for anonymous mappings
    BOOST_ASSUME( is_aligned( offset        , page_size           )                      ); // BSD does not impose this requirement but Linux and POSIX in general do

    auto const actual_address{ ::mmap( target_address, size, protection,
#   if defined( __linux__ ) && !defined( __ANDROID__ ) && 0 // investigate whether always wired
        MAP_HUGETLB |
#   endif
#   if defined( MAP_NOSYNC ) && 0 // TODO reconsider
        MAP_NOSYNC |
#   endif
        MAP_ALIGNED_SUPER | flags, // reconsider unconditional ALIGNED_SUPER: can cause mmap to fail https://man.freebsd.org/cgi/man.cgi?sektion=2&query=mmap
        file_handle,
        static_cast<off_t>( offset )
    ) };
    auto const succeeded{ actual_address != MAP_FAILED };
    if ( succeeded ) [[ likely ]]
    {
        BOOST_ASSUME( !target_address || ( actual_address == target_address ) || ( ( flags & MAP_FIXED ) == 0 ) );
        BOOST_ASSUME( is_aligned( actual_address, reserve_granularity ) );
        return actual_address;
    }

    return nullptr;
}

//------------------------------------------------------------------------------
} // posix
//------------------------------------------------------------------------------

BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
mapped_span
map
(
    handle::reference const source_mapping,
    flags ::viewing   const flags         ,
    std   ::uint64_t  const offset        ,
    std   ::size_t    const desired_size  ,
    [[ maybe_unused ]]
    bool              const file_backed // required for the WinNT backend
) noexcept
{
    /// \note mmap() explicitly rejects a zero length/desired_size, IOW
    /// unlike with MapViewOfFile() that approach cannot be used to
    /// automatically map the entire object - a valid size must be
    /// specified.
    /// http://man7.org/linux/man-pages/man2/mmap.2.html
    ///                               (30.09.2015.) (Domagoj Saric)

    auto const view_start
    {
        static_cast<mapped_span::value_type *>
        (
            posix::mmap
            (
                nullptr,
                desired_size,
                flags.protection,
                flags.flags,
                source_mapping,
                offset
            )
        )
    };

    return mapped_span{ view_start, view_start ? desired_size : 0 };
}

[[ gnu::cold, gnu::nothrow, clang::nouwtable ]]
void unmap( mapped_span const view )
{
    // An empty view is the state of a default-constructed or moved-from mapped
    // object: munmap()ing it merely fails with EINVAL. The WinNT backend has
    // always skipped empty views - do the same here so that the verification
    // below need not whitelist that failure (and thereby mask genuine ones).
    // The skip has to happen here: ::munmap() is a thin syscall stub which does
    // not validate its arguments (see handle.posix.hpp's close()), so an empty
    // view otherwise makes the round trip into the kernel just to come back
    // EINVAL.
    if ( !view.empty() )
    {
#   ifdef __EMSCRIPTEN__
        (void)::munmap( view.data(), view.size() );
#   else
        BOOST_VERIFY( ::munmap( view.data(), view.size() ) == 0 );
#   endif
    }
}

void unmap_partial( mapped_span const range ) noexcept { unmap( range ); }

void discard( mapped_span const range ) noexcept
{
    BOOST_VERIFY( ::madvise( range.data(), range.size(), MADV_DONTNEED ) == 0 );
    // https://www.man7.org/linux/man-pages/man2/madvise.2.html
    // MADV_FREE vs MADV_DONTNEED
    // https://lwn.net/Articles/590991
    // https://github.com/JuliaLang/julia/issues/51086
    // MADV_COLD, MADV_PAGEOUT
    // destructive MADV_REMOVE, MADV_FREE
}

namespace {
    __attribute__(( nothrow ))
    fallible_result<void> call_msync( mapped_span const range, int const flags ) {
        BOOST_ASSERT( is_aligned( range.data(), page_size ) );
        // It is OK (efficiency-wise) to call msync on the entire file regardless of how small the change is
        // https://stackoverflow.com/questions/68832263/does-msync-performance-depend-on-the-size-of-the-provided-range
        // EINVAL on OSX for empty range
        // https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/msync.2.html
        // According to https://linux.die.net/man/3/msync ensures
        // "synchronized I/O data integrity completion" which is defined to
        // include all data (i.e. including metadata) required for subsequent
        // reads of the data (https://stackoverflow.com/questions/37288453/calling-fsync2-after-close2/50167655#50167655)
        // This commit https://github.com/torvalds/linux/commit/7fc34a62ca4434a79c68e23e70ed26111b7a4cf8
        // also says as much.
        // https://wiki.postgresql.org/wiki/Fsync_Errors 'fsyncgate 2018'
        // https://stackoverflow.com/questions/42434872/writing-programs-to-cope-with-i-o-errors-causing-lost-writes-on-linux
        // https://lwn.net/Articles/684828 DAX, mmap(), and a "go faster" flag
        if ( ( ::msync( range.data(), range.size(), flags ) == 0 ) || range.empty() ) [[ likely ]]
            return err::success;
        return error{};
    }
}

// https://lwn.net/Articles/712467 The future of the page cache
void flush_async( mapped_span const range ) noexcept
{
    // According to https://www.man7.org/linux/man-pages/man2/msync.2.html
    // MS_ASYNC is a no-op on Linux yet it states that for portability it should
    // be specified. Also this article https://lwn.net/Articles/502612 points to
    // patches adding this functionality to Linux 'at some point'.
    call_msync( range, MS_ASYNC ).ignore_failure(); // fire-and-forget: result intentionally not observed
}

fallible_result<void> flush_blocking( mapped_span const range ) noexcept
{
    // MS_INVALIDATE should not be necessary on OSes with coherent/unified
    // caches? Even if it is necessary it is not clear that the user would want
    // it in all use cases (certainly not for mappings to which only a single
    // view ever exists)...
    // https://stackoverflow.com/questions/60547532/whats-the-exact-meaning-of-the-flag-ms-invalidate-in-msync
    // https://linux-fsdevel.vger.kernel.narkive.com/ytPKRHNt/munmap-msync-synchronization
    return call_msync( range, MS_SYNC /*| MS_INVALIDATE*/ ).propagate();
    // sync_file_range, fdatasync...
}
// msync(MS_SYNC) alone does not guarantee the data has reached the storage
// device: on Linux it is a range integrity sync equivalent to fdatasync (dirty
// pages + i_size, no dirent), but on macOS/*BSD msync/fsync do not flush the
// drive write cache - only F_FULLFSYNC does (Apple's fsync(2) man page; see
// also https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fsync.2.html).
// Without this extra step, callers relying on flush_blocking(range, file) for
// power-loss durability (not just process-crash durability) silently do not
// get it on macOS, even though the call "succeeds".
fallible_result<void> flush_blocking( mapped_span const range, file_handle::const_reference const source_file ) noexcept
{
#ifndef NDEBUG
    if ( flush_observer ) [[ unlikely ]] flush_observer( range, source_file );
    if ( flush_force_failure ) [[ unlikely ]] { errno = ENOSPC; return error{}; }
#endif
    if ( auto result{ flush_blocking( range )() }; !result ) [[ unlikely ]]
        return result.error();
#if defined( __APPLE__ )
    if ( ::fcntl( source_file.value, F_FULLFSYNC ) == -1 ) [[ unlikely ]]
        return error{};
#elif defined( __linux__ )
    // No extra fdatasync() here: the msync(MS_SYNC) call above already did the
    // equivalent work. The kernel's msync(MS_SYNC) handler (mm/msync.c) calls
    // vfs_fsync_range( file, start, end, /*datasync=*/1 ) - the exact same VFS
    // entry point fdatasync() itself calls. A second fdatasync() would just
    // repeat that traversal for zero additional durability.
#else
    // POSIX-compliant fallback for every other *nix (the BSDs, illumos, ...):
    // POSIX only requires msync() to make mmap'd stores visible to processes
    // that read the file directly - it does not require, and isn't verified
    // here to imply (unlike the Linux case above), that fsync()/fdatasync() on
    // the fd alone would already have picked up those pages without it. Keep
    // both calls until/unless a given OS's equivalence is actually confirmed.
    if ( ::fdatasync( source_file.value ) != 0 ) [[ unlikely ]]
        return error{};
#endif
    return err::success;
}

//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
