////////////////////////////////////////////////////////////////////////////////
///
/// \file flags/win32/opening.inl
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
#ifndef opening_inl__77AE8A6F_0E93_433B_A1F2_531BBBB353FC
#define opening_inl__77AE8A6F_0E93_433B_A1F2_531BBBB353FC
#pragma once
//------------------------------------------------------------------------------
#include "opening.hpp"

#include "boost/mmap/detail/impl_inline.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
namespace win32
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

static_assert( system_hints::random_access     ==   FILE_FLAG_RANDOM_ACCESS                               , "" );
static_assert( system_hints::sequential_access ==   FILE_FLAG_SEQUENTIAL_SCAN                             , "" );
static_assert( system_hints::avoid_caching     == ( FILE_FLAG_NO_BUFFERING   | FILE_FLAG_WRITE_THROUGH   ), "" );
static_assert( system_hints::temporary         == ( FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE ), "" );

BOOST_IMPL_INLINE
opening BOOST_CC_REG opening::create_for_opening_existing_objects
(
    access_privileges::object        const object_access,
    access_privileges::child_process const child_access,
    flags_t                                 const system_hints,
    bool                                    const truncate
)
{
    return create
    (
        access_privileges { object_access, child_access, access_privileges::system() },
        truncate
            ? named_object_construction_policy::open_and_truncate_existing
            : named_object_construction_policy::open_existing,
        system_hints
    );
}

//------------------------------------------------------------------------------
} // flags
//------------------------------------------------------------------------------
} // win32
//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
#endif // opening_inl
