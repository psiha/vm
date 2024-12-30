////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2023 - 2024.
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
#include "allocation.impl.hpp"
#include <psi/vm/allocation.hpp>
#include <psi/vm/align.hpp>
#include <psi/vm/detail/posix.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>

#include <cerrno>
#include <memory>
#include <utility>

#if defined( __APPLE__ )
#   include <mach/mach.h>
#   include <TargetConditionals.h>
#endif
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

void * mmap( void * const target_address, std::size_t const size, int const protection, int const flags ) noexcept
{
    auto const actual_address{ posix::mmap( target_address, size, protection, flags,
#   if defined( __APPLE__ ) && 0 // always wired
        VM_FLAGS_SUPERPAGE_SIZE_2MB,
#   else
        -1,
#   endif
        0
    ) };
    auto const succeeded{ actual_address != MAP_FAILED };
    if ( succeeded ) [[ likely ]]
    {
        BOOST_ASSERT( !target_address || ( actual_address == target_address ) || ( ( flags & MAP_FIXED ) == 0 ) );
        return std::assume_aligned<reserve_granularity>( actual_address );
    }

    return nullptr;
}


void * allocate( std::size_t & size ) noexcept
{
    size = __builtin_align_up( size, reserve_granularity );
    return mmap( nullptr, size, PROT_READ | PROT_WRITE, /*TODO rethink*/ MAP_NORESERVE );
}

void * reserve( std::size_t & size ) noexcept
{
    size = __builtin_align_up( size, reserve_granularity );
    return mmap( nullptr, size, PROT_NONE, /*TODO rethink*/ MAP_NORESERVE );
}
bool commit( void * const address, std::size_t const size ) noexcept
{
    BOOST_ASSUME( is_aligned( address, commit_granularity ) );
    BOOST_ASSUME( is_aligned( size   , commit_granularity ) );
    auto const success{ ::mprotect( address, size, PROT_READ | PROT_WRITE ) == 0 };
    BOOST_VERIFY( ::madvise( address, size, MADV_SEQUENTIAL | MADV_WILLNEED // TODO rethink: are these resonable default expectations for a freshly and explicitly commited range?
    #if defined( __linux__ ) && !defined( __ANDROID__ )
        | MADV_HUGEPAGE
    #endif // server Linux
    ) == 0 );
    return success;
}
[[ gnu::cold, gnu::nothrow, clang::nouwtable ]]
void decommit( void * const address, std::size_t const size ) noexcept
{
    BOOST_ASSUME( is_aligned( address, reserve_granularity ) );
    BOOST_ASSUME( is_aligned( size   , reserve_granularity ) );
    BOOST_VERIFY( ::mprotect( address, size, PROT_NONE ) == 0 );
#if 0 // should not be neccessary?
    BOOST_VERIFY
    (
        ::madvise( actual_address, size, MADV_FREE     ) == 0 ||
        ::madvise( actual_address, size, MADV_DONTNEED ) == 0
    );
#endif
}
[[ gnu::cold, gnu::nothrow, clang::nouwtable ]]
void free( void * const address, std::size_t const size ) noexcept
{
    BOOST_ASSUME( is_aligned( address, reserve_granularity ) );
    BOOST_ASSUME( is_aligned( size   , reserve_granularity ) );
    BOOST_VERIFY( ::munmap( address, size ) == 0 || !address || !size );
}

[[ gnu::cold, gnu::nothrow, clang::nouwtable ]]
bool allocate_fixed( void * const address, std::size_t const size, allocation_type const alloc_type ) noexcept
{
    // Cannot use MAP_FIXED as it silently overwrites existing mappings
    // https://stackoverflow.com/questions/14943990/overlapping-pages-with-mmap-map-fixed
    // Linux 4.7 has MAP_FIXED_NOREPLACE
    // https://github.com/torvalds/linux/commit/a4ff8e8620d3f4f50ac4b41e8067b7d395056843
#ifdef MAP_FIXED_NOREPLACE
    auto const noreplace_fixed_flag{ MAP_FIXED_NOREPLACE };
#else
    auto const noreplace_fixed_flag{ 0 };
#endif
    auto const adjusted_address{ mmap( address, size, std::to_underlying( alloc_type ), noreplace_fixed_flag ) };
    if ( adjusted_address == address ) [[ likely ]]
        return true;

    if ( adjusted_address )
    {
#   if !defined( __ANDROID__ ) // API level and kernel version chaos
        BOOST_ASSUME( !noreplace_fixed_flag );
#   endif
        BOOST_VERIFY( ::munmap( adjusted_address, size ) == 0 );
    }
    return false;
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
