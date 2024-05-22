////////////////////////////////////////////////////////////////////////////////
///
/// \file mapped_view.inl
/// ---------------------
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
#include <psi/vm/detail/win32.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>
#include <psi/vm/mapped_view/ops.hpp>

#include <boost/assert.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
mapped_span BOOST_CC_REG
map
(
    mapping::handle  const source_mapping,
    flags ::viewing  const flags         ,
    std   ::uint64_t const offset        , // ERROR_MAPPED_ALIGNMENT
    std   ::size_t   const desired_size
) noexcept
{
    // Implementation note:
    // Mapped views hold internal references to the mapping handles so we do
    // not need to hold/store them ourselves:
    // http://msdn.microsoft.com/en-us/library/aa366537(VS.85).aspx
    //                                    (26.03.2010.) (Domagoj Saric)

#if 1
    ULARGE_INTEGER const win32_offset{ .QuadPart = offset };
    auto const view_start
    {
        static_cast<mapped_span::value_type *>
        (
            ::MapViewOfFile
            (
                source_mapping,
                flags.map_view_flags,
                win32_offset.HighPart,
                win32_offset.LowPart,
                desired_size
            )
        )
    };
#else
    LARGE_INTEGER nt_offset{ .QuadPart = static_cast<LONGLONG>( offset ) };
    auto sz{ desired_size };
    void * view_startv{ nullptr };
    auto const success
    {
        nt::NtMapViewOfSection
        (
            source_mapping,
            nt::current_process,
            &view_startv,
            0,
            0,
            &nt_offset,
            &sz,
            nt::SECTION_INHERIT::ViewUnmap,
            0,
            PAGE_READWRITE
        )
    };
    BOOST_ASSERT( sz >= desired_size );
    auto const view_start{ static_cast<std::byte *>( view_startv ) };
#endif

    return
    {
        view_start,
        view_start ? desired_size : 0
    };
}

namespace
{
    [[ maybe_unused ]]
    auto mem_info( void * const address ) noexcept
    {
        MEMORY_BASIC_INFORMATION info;
        BOOST_VERIFY( ::VirtualQueryEx( nt::current_process, address, &info, sizeof( info ) ) == sizeof( info ) );
        return info;
    }
} // anonymous namespace

BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 )
void BOOST_CC_REG unmap( mapped_span const view ) noexcept
{
    BOOST_ASSERT_MSG
    (   // greater-or-equal because of the inability of Windows to do a proper unmap_partial
        // (so we do discard() calls and shrinking of the view-span only, while the actual
        // region remains in its original size)
        mem_info( view.data() ).RegionSize >= align_up( view.size(), commit_granularity ) || view.empty(),
        "TODO: implement a loop to handle concatenated ranges"
        // see mapped_view::expand, Windows does not allow partial unmaps or merging of mapped views/ranges
    );
    BOOST_VERIFY( ::UnmapViewOfFile( view.data() ) || view.empty() );
}

void unmap_partial( mapped_span const range ) noexcept
{
    // Windows does not offer this functionality (altering VMAs), the best
    // we can do is:
    discard( range );
    // TODO add a mem_info query to see if there maybe is  (sub)range we
    // can actually fully unmap here (connected to the todo in umap and expand)
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
    BOOST_VERIFY( ::FlushViewOfFile( range.data(), range.size() ) );
}

void flush_blocking( mapped_span const range, file_handle::reference const source_file ) noexcept
{
    flush_async( range );
    BOOST_VERIFY( ::FlushFileBuffers( source_file ) );
}

//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
