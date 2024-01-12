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
#include <psi/vm/flags/opening.posix.hpp>
#include <psi/vm/detail/impl_selection.hpp>
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------
PSI_VM_POSIX_INLINE namespace posix
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

opening opening::create
(
    access_privileges                const ap,
    named_object_construction_policy const construction_policy,
    flags_t                          const combined_system_hints
) noexcept
{
    auto const oflag{ ap.oflag() | static_cast<flags_t>( construction_policy ) | combined_system_hints };
    auto const pmode{ ap.pmode()                                                                       };

    return { .oflag = oflag, .pmode = static_cast<flags_t>( pmode ) };
}


opening opening::create_for_opening_existing_objects
(
    access_privileges::object        const object_access,
    access_privileges::child_process const child_access,
    flags_t                          const combined_system_hints,
    bool                             const truncate
) noexcept
{
    return create
    (
        access_privileges { object_access, child_access, {0} },
        truncate
            ? named_object_construction_policy::open_and_truncate_existing
            : named_object_construction_policy::open_existing,
        combined_system_hints
    );
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
