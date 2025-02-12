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
    [[ maybe_unused ]] bool const file_backed // required for the WinNT backend
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
    [[ maybe_unused ]] auto munmap_result{ ::munmap( view.data(), view.size() ) };
#ifndef __EMSCRIPTEN__
    BOOST_VERIFY
    (
        ( munmap_result == 0 ) ||
        ( view.empty() && ( !view.data() || errno == EINVAL ) )
    );
#endif
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
    void call_msync( mapped_span const range, int const flags ) {
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
        BOOST_VERIFY( ::msync( range.data(), range.size(), flags ) == 0 || range.empty() );
    }
}

// https://lwn.net/Articles/712467 The future of the page cache
void flush_async( mapped_span const range ) noexcept
{
    // According to https://www.man7.org/linux/man-pages/man2/msync.2.html
    // MS_ASYNC is a no-op on Linux yet it states that for portability it should
    // be specified. Also this article https://lwn.net/Articles/502612 points to
    // patches adding this functionality to Linux 'at some point'.
    call_msync( range, MS_ASYNC );
}

void flush_blocking( mapped_span const range ) noexcept
{
    // MS_INVALIDATE should not be necessary on OSes with coherent/unified
    // caches? Even if it is necessary it is not clear that the user would want
    // it in all use cases (certainly not for mappings to which only a single
    // view ever exists)...
    // https://stackoverflow.com/questions/60547532/whats-the-exact-meaning-of-the-flag-ms-invalidate-in-msync
    // https://linux-fsdevel.vger.kernel.narkive.com/ytPKRHNt/munmap-msync-synchronization
    call_msync( range, MS_SYNC /*| MS_INVALIDATE*/ );
    // sync_file_range, fdatasync...
}
void flush_blocking( mapped_span const range, file_handle::const_reference ) noexcept { flush_blocking( range ); }

//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
