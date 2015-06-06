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

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#include "windows.h"
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

static_assert( opening<win32>::access_rights::read    == GENERIC_READ   , "" );
static_assert( opening<win32>::access_rights::write   == GENERIC_WRITE  , "" );
static_assert( opening<win32>::access_rights::all     == GENERIC_ALL    , "" );

static_assert( opening<win32>::system_hints::random_access     ==   FILE_FLAG_RANDOM_ACCESS                               , "" );
static_assert( opening<win32>::system_hints::sequential_access ==   FILE_FLAG_SEQUENTIAL_SCAN                             , "" );
static_assert( opening<win32>::system_hints::avoid_caching     == ( FILE_FLAG_NO_BUFFERING   | FILE_FLAG_WRITE_THROUGH   ), "" );
static_assert( opening<win32>::system_hints::temporary         == ( FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE ), "" );

static_assert( opening<win32>::new_system_object_public_access_rights::read    == FILE_ATTRIBUTE_READONLY, "" );
static_assert( opening<win32>::new_system_object_public_access_rights::write   == FILE_ATTRIBUTE_NORMAL  , "" );
static_assert( opening<win32>::new_system_object_public_access_rights::execute == FILE_ATTRIBUTE_READONLY, "" );

BOOST_IMPL_INLINE
opening<win32> opening<win32>::create
(
    flags_t                           const handle_access_flags   ,
    system_object_construction_policy const open_flags            ,
    flags_t                           const system_hints          ,
    flags_t                           const on_construction_rights
)
{
    return
    {
        handle_access_flags, // desired_access
        open_flags, // creation_disposition
        system_hints
            |
        (
            ( on_construction_rights & FILE_ATTRIBUTE_NORMAL )
                ? FILE_ATTRIBUTE_NORMAL
                : on_construction_rights
        ) // flags_and_attributes
    };
}


BOOST_IMPL_INLINE
opening<win32> opening<win32>::create_for_opening_existing_files
(
    flags_t const handle_access_flags,
    flags_t const system_hints,
    bool    const truncate
)
{
    return create
    (
        handle_access_flags,
        truncate
            ? system_object_construction_policy::open_and_truncate_existing
            : system_object_construction_policy::open_existing,
        system_hints,
        0
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
