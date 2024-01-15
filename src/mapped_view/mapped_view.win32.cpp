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
#include "mapper.hpp"

#include <psi/vm/align.hpp>
#include <psi/vm/detail/win32.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>

#include <boost/assert.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
mapped_span BOOST_CC_REG
mapper::map
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
    sz = 0;
    void * view_startv{ nullptr };
    auto const success
    {
        nt::NtMapViewOfSection
        (
            source_mapping,
            ::GetCurrentProcess(),
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
    auto const view_start{ static_cast< std::byte * >( view_startv ) };
#endif

    return
    {
        view_start,
        view_start ? desired_size : 0
    };
}

BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 )
void BOOST_CC_REG mapper::unmap( mapped_span const view ) noexcept
{
    BOOST_VERIFY( ::UnmapViewOfFile( view.data() ) || view.empty() );
}

void mapper::shrink( mapped_span const view, std::size_t const target_size ) noexcept
{
    // VirtualFree (wrapped by decommit) does not work for mapped views (only for VirtualAlloced memory)
    BOOST_VERIFY
    (
        ::DiscardVirtualMemory
        (
            align_up  ( view.data() + target_size, commit_granularity ),
            align_down( view.size() - target_size, commit_granularity )
        )
    );
}

void mapper::flush( mapped_span const view ) noexcept
{
    BOOST_VERIFY( ::FlushViewOfFile( view.data(), view.size() ) == 0 );
}

//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
