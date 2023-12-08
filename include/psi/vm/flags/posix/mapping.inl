////////////////////////////////////////////////////////////////////////////////
///
/// \file flags/posix/mapping.inl
/// -----------------------------
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
#ifndef mapping_inl__79CF82B8_F71B_4C75_BE77_98F4FB8A7FFA
#define mapping_inl__79CF82B8_F71B_4C75_BE77_98F4FB8A7FFA
#pragma once
//------------------------------------------------------------------------------
#include "mapping.hpp"

#include "psi/vm/detail/impl_inline.hpp"
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------
inline namespace posix
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

PSI_VM_IMPL_INLINE
viewing viewing::create
(
    access_privileges::object const access_flags,
    share_mode                const share_mode
) noexcept
{
    return
    {
        .protection = access_flags.protection(),
        .flags      = static_cast<flags_t>( share_mode )
        #ifdef MAP_UNINITIALIZED
            | MAP_UNINITIALIZED
        #endif // MAP_UNINITIALIZED
    };
}

//------------------------------------------------------------------------------
} // flags
//------------------------------------------------------------------------------
} // posix
//------------------------------------------------------------------------------
} // vm
//------------------------------------------------------------------------------
} // psi
//------------------------------------------------------------------------------
#endif // mapping.inl
