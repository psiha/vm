////////////////////////////////////////////////////////////////////////////////
///
/// \file mapped_view.win32.cpp
/// ---------------------------
///
/// Copyright (c) Domagoj Saric 2010 - 2025.
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
#include "mapped_view.win32.hpp"
#include "../allocation/allocation.impl.hpp"

#include <psi/vm/align.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>
#include <psi/vm/mapped_view/ops.hpp>

#include <boost/assert.hpp>

#ifndef NDEBUG
#include <cstdio> // for fputs
#endif

#pragma comment( lib, "onecore.lib" ) // for MapViewOfFile2
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

[[ using gnu: nothrow, sysv_abi, cold ]] BOOST_ATTRIBUTES( BOOST_MINSIZE )
mapped_span
windows_mmap
(
    mapping::handle     const source_mapping  ,
    void *              const desired_position,
    std    ::size_t     const desired_size    ,
    std    ::uint64_t   const offset          ,
    flags  ::viewing    const flags           ,
    mapping_object_type const allocation_type
) noexcept
{
    // Windows accepts zero as a valid argument indicating "map the entire file/
    // section" while POSIX mmap does not support this: for the time being treat
    // our API/this wrapper as also not supporting it (have the user/calling
    // code decide what behaviour it wants).
    BOOST_ASSUME( desired_size );

    BOOST_ASSUME( is_aligned( offset          , reserve_granularity ) );
    BOOST_ASSUME( is_aligned( desired_position, reserve_granularity ) );

    // Implementation note:
    // Mapped views hold internal references to the mapping handles so we do
    // not need to hold/store them ourselves:
    // http://msdn.microsoft.com/en-us/library/aa366537(VS.85).aspx
    //                                    (26.03.2010.) (Domagoj Saric)
#if 0
    auto const view_start
    {
        static_cast<mapped_span::value_type *>
        (
            ::MapViewOfFile2
            (
                source_mapping,
                nt::current_process,
                offset,
                desired_position,
                desired_size,
                std::to_underlying( allocation_type ),
                flags.page_protection
            )
        )
    };
    return
    {
        view_start,
        view_start ? desired_size : 0
    };
#else
    auto const writable{ ( flags.page_protection & ( PAGE_READWRITE | PAGE_EXECUTE_READWRITE ) ) != 0 }; //...mrmlj...simplify all these hacks (for read only file mappings the 'reserve and have NtExtendSection auto-commit' logic does not seem to work)
    auto const allocation_aligned_size{ align_up( desired_size, reserve_granularity ) };
    auto const actual_size{ writable ? allocation_aligned_size : align_up( desired_size, commit_granularity ) };
    auto          sz         { writable ? allocation_aligned_size : desired_size };
    void *        view_startv{ desired_position };
    LARGE_INTEGER nt_offset  { .QuadPart = static_cast<LONGLONG>( offset ) };
    auto const success
    {
        nt::NtMapViewOfSection
        (
            source_mapping,
            nt::current_process,
            &view_startv,
            0,
            sz,
            &nt_offset,
            &sz,
            nt::SECTION_INHERIT::ViewUnmap,
            writable ? std::to_underlying( allocation_type ) : 0,
            flags.page_protection
        )
    };
    if ( success == nt::STATUS_SUCCESS ) [[ likely ]]
    {
        BOOST_ASSUME( sz          == actual_size );
        BOOST_ASSUME( view_startv != nullptr     );
        // Return the requested desired_size rather than the actual (page
        // aligned size) as calling code can rely on this size e.g. to track the
        // size of a mapped file.
        return { static_cast<std::byte *>( view_startv ), desired_size };
    }
    else
    {
#   ifndef NDEBUG
        ::SetLastError( ::RtlNtStatusToDosError( success ) );
#   endif
        BOOST_ASSUME( sz          == actual_size                      );
        BOOST_ASSUME( view_startv == desired_position                 );
        BOOST_ASSUME( success     == nt::STATUS_CONFLICTING_ADDRESSES );
        return {};
    }
#endif
} // windows_mmap

mapped_span
map
(
    mapping::handle     const source_mapping,
    flags  ::viewing    const flags         ,
    std    ::uint64_t   const offset        , // ERROR_MAPPED_ALIGNMENT
    std    ::size_t     const desired_size  ,
    bool                const file_backed
) noexcept
{
    return windows_mmap( source_mapping, nullptr, desired_size, offset, flags, file_backed ? mapping_object_type::file : mapping_object_type::memory );
}

// defined in allocation.win32.cpp
std::size_t mem_region_size( void * const address ) noexcept;

namespace
{
    BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 ) BOOST_NOINLINE
    void unmap_concatenated( void * address, std::size_t size ) noexcept
    {
        // emulate support for adjacent/concatenated regions (as supported by mmap)
        // (duplicated logic from free() in allocation.win32.cpp)
        for ( ;; )
        {
            auto const region_size{ mem_region_size( address ) };
            BOOST_ASSUME( region_size <= align_up( size, reserve_granularity ) );
            BOOST_VERIFY( ::UnmapViewOfFile( address ) );
            if ( region_size >= size )
                break;
            add( address, region_size );
            sub( size   , region_size );
        }
    }
} // anonymous namespace

BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 )
void unmap( mapped_span const view ) noexcept
{
    if ( !view.empty() )
        unmap_concatenated( view.data(), view.size() );
}

void unmap_partial( mapped_span const range ) noexcept
{
    // Windows does not offer this functionality (altering VMAs), the best
    // we can do is:
    discard( range );
    // TODO add a mem_info query to see if there maybe is a (sub)range we can
    // actually fully unmap here (connected to the todo in umap and expand)
}

void discard( mapped_span const range ) noexcept
{
    // A survey of the various ways of declaring pages of memory to be uninteresting
    // https://devblogs.microsoft.com/oldnewthing/20170113-00/?p=95185
    // https://chromium.googlesource.com/chromium/src.git/+/refs/heads/main/docs/memory/key_concepts.md https://issues.chromium.org/issues/40522456
    // VirtualFree (wrapped by decommit) does not work for mapped views (only for VirtualAlloced memory)
    BOOST_VERIFY( ::DiscardVirtualMemory( range.data(), range.size() ) );
}

void flush_async( mapped_span const range ) noexcept
{
    // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwflushvirtualmemory
    if ( !::FlushViewOfFile( range.data(), range.size() ) ) [[ unlikely ]]
    {
#   ifndef NDEBUG //...mrmlj...catching ghosts
        std::fputs( err::make_exception( err::last_win32_error{} ).what(), stderr );
#   endif
    }
}

void flush_blocking( mapped_span const range, file_handle::const_reference const source_file ) noexcept
{
    flush_async( range );
    // https://devblogs.microsoft.com/oldnewthing/20100909-00/?p=12913 Flushing your performance down the drain, that is
    BOOST_VERIFY( ::FlushFileBuffers( source_file.value ) );
}

//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
