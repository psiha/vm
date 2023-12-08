////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping.hpp
/// -----------------
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
#ifndef mapping_hpp__8B2CEDFB_C87C_4AA4_B9D0_8EF0A42825F2
#define mapping_hpp__8B2CEDFB_C87C_4AA4_B9D0_8EF0A42825F2
#pragma once
//------------------------------------------------------------------------------
#include "psi/vm/handles/win32/handle.hpp"
#include "psi/vm/flags/win32/mapping.hpp"

#include <boost/config.hpp>
//#include <boost/winapi/system.hpp> //...broken?
#include <psi/vm/detail/win32.hpp>

#include <cstdint>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winline-namespace-reopened-noninline"
#endif

//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------
namespace win32
{
//------------------------------------------------------------------------------

struct mapping
    :
    handle
{
    using reference = mapping const &;

    static bool const owns_parent_handle = true;

    constexpr mapping( native_handle_t const native_handle, flags::viewing const view_mapping_flags_param ) noexcept
        : handle( native_handle ), view_mapping_flags( view_mapping_flags_param ) {}

    bool is_read_only() const noexcept { return ( view_mapping_flags.map_view_flags & flags::mapping::access_rights::write ) == 0; }

    flags::viewing const view_mapping_flags;
}; // struct mapping

#ifdef PAGE_SIZE
std::uint16_t const page_size( PAGE_SIZE );
#else
inline std::uint16_t const page_size
(
    ([]() noexcept
    {
        //boost::winapi::GetSystemInfo broken?
        //boost::winapi::SYSTEM_INFO_ info;
        //boost::winapi::GetNativeSystemInfo( &info );
        ::SYSTEM_INFO info; ::GetSystemInfo( &info );
        BOOST_ASSUME( info.dwPageSize == 4096 );
        return static_cast<std::uint16_t>( info.dwPageSize );
    })()
);
#endif // PAGE_SIZE
#if defined( BOOST_MSVC )
#   pragma warning( push )
#   pragma warning( disable : 4553 ) // "'==': operator has no effect; did you intend '='?"
#endif // BOOST_MSVC
inline std::uint32_t const allocation_granularity
(
    ([]() noexcept
    {
        ::SYSTEM_INFO info; ::GetSystemInfo( &info );
        BOOST_LIKELY( info.dwAllocationGranularity     == 65536 );
        BOOST_ASSUME( info.dwAllocationGranularity % 2 == 0     );
        return info.dwAllocationGranularity;
    })()
);
#if defined( BOOST_MSVC )
#   pragma warning( pop )
#endif // BOOST_MSVC

//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif // mapping_hpp
