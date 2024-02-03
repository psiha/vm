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
#pragma once

#include <psi/vm/handles/handle.win32.hpp>
#include <psi/vm/flags/mapping.win32.hpp>
#include <psi/vm/error/error.nt.hpp>

#include <psi/err/fallible_result.hpp>

#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace win32
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

    static bool constexpr retains_parent_handle              = true ;
    static bool constexpr create_mapping_can_set_source_size = true ;
    static bool constexpr supports_zero_sized_mappings       = false; // Windows does not support zero sized mappings
    static bool constexpr supports_zero_sized_views          = false;

    constexpr mapping(            ) noexcept = default;
    constexpr mapping( mapping && ) noexcept = default;

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


std::uint64_t                         get_size( mapping::const_handle                         ) noexcept;
err::fallible_result<void, nt::error> set_size( mapping::      handle, std::uint64_t new_size ) noexcept;

//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
