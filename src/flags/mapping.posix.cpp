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
#include <psi/vm/flags/mapping.posix.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace posix
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

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
#       ifdef MAP_UNINITIALIZED
            | MAP_UNINITIALIZED
#       endif // MAP_UNINITIALIZED
    };
}

//------------------------------------------------------------------------------
} // flags
//------------------------------------------------------------------------------
} // posix
//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
