////////////////////////////////////////////////////////////////////////////////
///
/// \file flags/posix/opening.inl
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
#ifndef open_flags_inl__0F422517_D9AA_4E3F_B3E4_B139021D068E
#define open_flags_inl__0F422517_D9AA_4E3F_B3E4_B139021D068E
#pragma once
//------------------------------------------------------------------------------
#include "opening.hpp"

#include "boost/mmap/detail/impl_inline.hpp"

#include "boost/assert.hpp"
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
opening<posix> opening<posix>::create
(
    flags_t                           const handle_access_flags   ,
    system_object_construction_policy const open_flags            ,
    flags_t                           const system_hints          ,
    flags_t                           const on_construction_rights
) noexcept
{
    //...zzz...use fadvise...
    // http://stackoverflow.com/questions/2299402/how-does-one-do-raw-io-on-mac-os-x-ie-equivalent-to-linuxs-o-direct-flag

    unsigned int const oflag
    (
        ( ( handle_access_flags == ( O_RDONLY | O_WRONLY ) ) ? O_RDWR : handle_access_flags )
                |
            static_cast<flags_t>( open_flags )
                |
            system_hints
    );

    unsigned int const pmode( on_construction_rights );

    return
    {
        .oflag = static_cast<flags_t>( oflag ),
        .pmode = static_cast<flags_t>( pmode )
    };
}


BOOST_IMPL_INLINE
opening<posix> opening<posix>::create_for_opening_existing_files
(
    flags_t const handle_access_flags,
    flags_t const system_hints,
    bool    const truncate
) noexcept
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
#endif // open_flags_inl
