////////////////////////////////////////////////////////////////////////////////
///
/// \file shared_memory/flags.win32.hpp
/// -----------------------------------
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

#include "boost/config.hpp"

#include <psi/vm/detail/impl_selection.hpp>
#include <psi/vm/flags/mapping.win32.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace win32
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

struct shared_memory;

struct shared_memory : mapping
{
    enum struct system_hints
    {
        default_                   = 0x8000000,
        only_reserve_address_space = 0x4000000
    }; // struct system_hints

    static shared_memory BOOST_CC_REG create
    (
        access_privileges,
        named_object_construction_policy,
        system_hints system_hint
    ) noexcept;
}; // struct shared_memory

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
