////////////////////////////////////////////////////////////////////////////////
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
#include <psi/vm/flags/opening.hpp>
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

static_assert( system_hints::random_access     ==   FILE_FLAG_RANDOM_ACCESS                               , "" );
static_assert( system_hints::sequential_access ==   FILE_FLAG_SEQUENTIAL_SCAN                             , "" );
static_assert( system_hints::avoid_caching     == ( FILE_FLAG_NO_BUFFERING   | FILE_FLAG_WRITE_THROUGH   ), "" );
static_assert( system_hints::temporary         == ( FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE ), "" );

opening BOOST_CC_REG opening::create_for_opening_existing_objects
(
    access_privileges::object        const object_access,
    access_privileges::child_process const child_access,
    flags_t                          const system_hints,
    bool                             const truncate
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
} // psi::vm
//------------------------------------------------------------------------------
