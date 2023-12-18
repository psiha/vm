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
#include "psi/vm/error/error.hpp"
#include "psi/vm/error/nt/error.hpp"

#include <boost/config.hpp>
//#include <boost/winapi/system.hpp> //...broken?
#include <psi/vm/detail/nt.hpp>
#include <psi/vm/detail/win32.hpp>

#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm::win32
{
//------------------------------------------------------------------------------

struct [[ clang::trivial_abi ]] mapping
    :
    handle
{
    using       reference = mapping       &;
    using const_reference = mapping const &;

    using       handle = handle_ref<mapping, false>;
    using const_handle = handle_ref<mapping, true >;

    static bool const retains_parent_handle = true;

    constexpr mapping() = default;
    constexpr mapping( native_handle_t const native_handle, flags::viewing const view_mapping_flags_param ) noexcept
        : vm::handle( native_handle ), view_mapping_flags( view_mapping_flags_param ) {}

    bool is_read_only() const noexcept { return ( view_mapping_flags.map_view_flags & flags::mapping::access_rights::write ) == 0; }

    void operator=( mapping && source ) noexcept
    {
        vm::handle::operator=( std::move( source ) );
        view_mapping_flags = source.view_mapping_flags;
        source.view_mapping_flags = {};
    }

    flags::viewing view_mapping_flags{};
}; // struct mapping


inline std::uint64_t get_size( mapping::const_handle const mapping_handle ) noexcept
{
    using namespace nt;
    SECTION_BASIC_INFORMATION info;
    auto const result{ NtQuerySection( mapping_handle.value, SECTION_INFORMATION_CLASS::SectionBasicInformation, &info, sizeof( info ), nullptr ) };
    BOOST_VERIFY( NT_SUCCESS( result ) );
    return info.SectionSize.QuadPart;
}

inline err::fallible_result<void, nt::error> set_size( mapping::handle const mapping_handle, std::uint64_t const new_size ) noexcept
{
    using namespace nt;
    LARGE_INTEGER ntsz{ .QuadPart = static_cast<LONGLONG>( new_size ) };
    auto const result{ NtExtendSection( mapping_handle.value, &ntsz ) };
    if ( !NT_SUCCESS( result ) ) [[ unlikely ]]
        return result;

    BOOST_ASSERT( ntsz.QuadPart >= new_size );
    if ( ntsz.QuadPart > new_size )
    {
        BOOST_ASSERT( ntsz.QuadPart == get_size( mapping_handle ) );
        BOOST_ASSERT( ntsz.QuadPart - new_size < 8 /*what!?!*/ );
    }
    return err::success;
}

//------------------------------------------------------------------------------
} // namespace psi::vm::win32
//------------------------------------------------------------------------------
#endif // mapping_hpp
