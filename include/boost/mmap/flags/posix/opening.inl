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
#ifndef opening_inl__0F422517_D9AA_4E3F_B3E4_B139021D068E
#define opening_inl__0F422517_D9AA_4E3F_B3E4_B139021D068E
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
opening<posix> BOOST_CC_REG opening<posix>::create
(
    access_privileges<posix>                            const ap,
    named_object_construction_policy<posix>::value_type const construction_policy,
    flags_t                                             const system_hints
) noexcept
{
    auto const oflag( ap.oflag() | construction_policy | system_hints );
    auto const pmode( ap.pmode()                                      );

    return { .oflag = oflag, .pmode = pmode };
}


BOOST_IMPL_INLINE
opening<posix> BOOST_CC_REG opening<posix>::create_for_opening_existing_objects
(
    access_privileges<posix>::object        const object_access,
    access_privileges<posix>::child_process const child_access,
    flags_t                                 const system_hints,
    bool                                    const truncate
) noexcept
{
    return create
    (
        { object_access, child_access, 0 },
         truncate
            ? named_object_construction_policy<posix>::open_and_truncate_existing
            : named_object_construction_policy<posix>::open_existing,
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
