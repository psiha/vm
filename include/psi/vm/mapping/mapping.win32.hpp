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

// The first two includes have to come in that order otherwise we get #error: "No Target Architecture" in winnt.h@169
#include <psi/vm/flags/mapping.win32.hpp>
#include <psi/vm/error/error.nt.hpp>
#include <psi/vm/handles/handle.win32.hpp>
#include <psi/vm/mappable_objects/file/handle.hpp>

#include <psi/err/fallible_result.hpp>

#include <boost/assert.hpp>

#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace win32
{
//------------------------------------------------------------------------------

#ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wignored-attributes"
#endif

struct [[ clang::trivial_abi ]] mapping
    :
    handle
{
    using       reference = mapping       &;
    using const_reference = mapping const &;

    using       handle = handle_ref<mapping, false>;
    using const_handle = handle_ref<mapping, true >;

    static bool constexpr create_mapping_can_set_source_size = true ;
    static bool constexpr supports_zero_sized_mappings       = false; // Windows does not support zero sized mappings
    static bool constexpr supports_zero_sized_views          = false;
    static bool constexpr views_downsizeable                 = false;

    constexpr mapping(            ) noexcept = default;
    constexpr mapping( mapping && ) noexcept = default;

    constexpr mapping( native_handle_t const native_mapping_handle, flags::viewing const _view_mapping_flags, flags::flags_t const _create_mapping_flags = 0, file_handle && _file = {} ) noexcept
        : vm::handle{ native_mapping_handle }, view_mapping_flags{ _view_mapping_flags }, create_mapping_flags{ _create_mapping_flags }, file{ std::move( _file ) } {}

    bool is_read_only() const noexcept { return ( view_mapping_flags.map_view_flags & flags::mapping::access_rights::write ) == 0; }

    bool is_file_based() const noexcept { return static_cast<bool>( file ); }

    file_handle::      reference underlying_file()       noexcept { BOOST_ASSERT( is_file_based() ); return file; }
    file_handle::const_reference underlying_file() const noexcept { BOOST_ASSERT( is_file_based() ); return file; }

    void operator=( mapping && source ) noexcept
    {
        vm::handle::operator=( std::move( source ) );
        file                 = std::move( source.file );
          view_mapping_flags = source.  view_mapping_flags;
        create_mapping_flags = source.create_mapping_flags;
        source.  view_mapping_flags = {};
        source.create_mapping_flags = {};
    }

    flags::viewing view_mapping_flags{};
    // members below are only required for downsizeable mappings (and is_file_based() info)
    // TODO create a separate mapping type
    flags::flags_t create_mapping_flags;
    file_handle    file;
}; // struct mapping

#ifdef __clang__
#    pragma clang diagnostic pop
#endif

std::uint64_t                         get_size( mapping::const_handle             ) noexcept;
err::fallible_result<void, nt::error> set_size( mapping &, std::uint64_t new_size ) noexcept;

//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
