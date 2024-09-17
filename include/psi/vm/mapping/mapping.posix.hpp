////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping.hpp
/// -----------------
///
/// Copyright (c) Domagoj Saric 2011 - 2024.
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

#include <psi/vm/error/error.hpp>
#include <psi/vm/flags/mapping.posix.hpp>
#include <psi/vm/handles/handle.posix.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
PSI_VM_POSIX_INLINE
namespace posix
{
//------------------------------------------------------------------------------

struct [[ clang::trivial_abi ]] mapping
    :
    handle
{
    using const_handle = posix::handle::const_reference;
    using       handle = posix::handle::      reference;

    using const_reference = mapping const &;
    using       reference = mapping       &;

    static bool constexpr create_mapping_can_set_source_size = false;
    static bool constexpr supports_zero_sized_mappings       = true ; // simply because there is actually no intermediate mapping object
    static bool constexpr views_downsizeable                 = true ;

    constexpr mapping(            ) noexcept = default;
    constexpr mapping( mapping && ) noexcept = default;

    template <typename FileHandle>
    constexpr mapping( FileHandle && fd, flags::viewing const & view_mapping_flags_param, std::size_t const size ) noexcept
        : vm::handle{ std::forward<FileHandle>( fd ) }, view_mapping_flags{ view_mapping_flags_param }, maximum_size{ size } {}

    bool is_read_only() const noexcept
    {
        return
        (
            static_cast< std::uint32_t >( view_mapping_flags.protection ) &
            ( flags::access_privileges::write | flags::access_privileges::readwrite )
        ) == 0;
    }

    bool is_file_based() const noexcept { return posix::handle::operator bool(); }

    bool is_anonymous() const noexcept { return view_mapping_flags.flags & MAP_ANONYMOUS; }

    posix::handle::      reference underlying_file()       noexcept { BOOST_ASSERT( is_file_based() ); return *this; }
    posix::handle::const_reference underlying_file() const noexcept { BOOST_ASSERT( is_file_based() ); return *this; }

    constexpr mapping & operator=( mapping && source ) noexcept
    {
        vm::handle::operator=( std::move( source ) );
        view_mapping_flags = source.view_mapping_flags;
        maximum_size       = source.maximum_size;
        source.view_mapping_flags = {};
        source.maximum_size       = {};
        return *this;
    }

    // override Handle operator bool to allow for anonymous mappings
    explicit operator bool() const noexcept { return posix::handle::operator bool() || is_anonymous(); }

    flags::viewing view_mapping_flags{};
    std  ::size_t  maximum_size      {};
}; // struct mapping

// fwd declarations for file_handle functions which work for mappings as well ('everything is a file')
err::fallible_result<void, error> set_size( handle::      reference, std::uint64_t desired_size ) noexcept;
std::uint64_t                     get_size( handle::const_reference                             ) noexcept;

// (temporary) special overloads to handle anonymous mappings (until a dedicated mapping type sees the light of day)
err::fallible_result<void, error> set_size( mapping       &, std::size_t desired_size ) noexcept;
std::size_t                       get_size( mapping const &                           ) noexcept;

//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
