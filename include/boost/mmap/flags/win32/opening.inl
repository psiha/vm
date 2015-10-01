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
namespace flags
{
//------------------------------------------------------------------------------

static_assert( system_hints<win32>::random_access     ==   FILE_FLAG_RANDOM_ACCESS                               , "" );
static_assert( system_hints<win32>::sequential_access ==   FILE_FLAG_SEQUENTIAL_SCAN                             , "" );
static_assert( system_hints<win32>::avoid_caching     == ( FILE_FLAG_NO_BUFFERING   | FILE_FLAG_WRITE_THROUGH   ), "" );
static_assert( system_hints<win32>::temporary         == ( FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE ), "" );

BOOST_IMPL_INLINE
opening<win32> BOOST_CC_REG opening<win32>::create_for_opening_existing_objects
(
    access_privileges<win32>::object        const object_access,
    access_privileges<win32>::child_process const child_access,
    flags_t                                 const system_hints,
    bool                                    const truncate
)
{
    return create
    (
        access_privileges<win32> { object_access, child_access, access_privileges<win32>::system() },
        truncate
            ? named_object_construction_policy<win32>::open_and_truncate_existing
            : named_object_construction_policy<win32>::open_existing,
        system_hints
    );
}

//------------------------------------------------------------------------------
} // flags
//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
#endif // opening_inl
