////////////////////////////////////////////////////////////////////////////////
///
/// \file flags/posix/mapping.inl
/// -----------------------------
///
/// Copyright (c) Domagoj Saric 2010 - 2015.
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

#include "boost/mmap/detail/impl_inline.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

BOOST_IMPL_INLINE
viewing<posix> viewing<posix>::create
(
    access_privileges<posix>::object const access_flags,
    share_mode                       const share_mode
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
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
#endif // mapping.inl
