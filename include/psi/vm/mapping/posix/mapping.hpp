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
#ifndef mapping_hpp__99837E03_86B1_42F5_A57D_69A6E828DD08
#define mapping_hpp__99837E03_86B1_42F5_A57D_69A6E828DD08
#pragma once
//------------------------------------------------------------------------------
#include "psi/vm/error/error.hpp"
#include "psi/vm/flags/posix/mapping.hpp"
#include "psi/vm/handles/posix/handle.hpp"

#include <boost/config/detail/suffix.hpp>
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
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

    static bool constexpr retains_parent_handle              = false;
    static bool constexpr create_mapping_can_set_source_size = false;

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

    constexpr mapping & operator=( mapping && source ) noexcept
    {
        vm::handle::operator=( std::move( source ) );
        view_mapping_flags = source.view_mapping_flags;
        maximum_size       = source.maximum_size;
        source.view_mapping_flags = {};
        source.maximum_size       = {};
        return *this;
    }

    flags::viewing view_mapping_flags{};
    std  ::size_t  maximum_size      {};
}; // struct mapping

// fwd declarations for file_handle functions which work for mappings as well ('everything is a file')
err::fallible_result<void, error> BOOST_CC_REG set_size( handle::      reference, std::uint64_t desired_size ) noexcept;
std::uint64_t                     BOOST_CC_REG get_size( handle::const_reference                             ) noexcept;

//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------
#endif // mapping_hpp
