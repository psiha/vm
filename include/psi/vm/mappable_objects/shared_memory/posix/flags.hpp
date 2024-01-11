////////////////////////////////////////////////////////////////////////////////
///
/// \file shared_memory/posix/flags.hpp
/// -----------------------------------
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
#pragma once

#include <psi/vm/detail/impl_selection.hpp>
#include <psi/vm/detail/posix.hpp>
#include <psi/vm/flags/mapping.posix.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
PSI_VM_POSIX_INLINE
namespace posix
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

using flags_t = int;

struct shared_memory
{
    struct system_hints
    {
        /// \note The "only_reserve_address_space" flag has different semantics
        /// on POSIX systems (as opposed to Windows): the mapped reagion can
        /// actually be immediately accessed - if there are enough free physical
        /// memory pages - otherwise we get a SIGSEGV which can luckly be
        /// 'caught':
        /// http://stackoverflow.com/questions/3012237/handle-sigsegv-in-linux
        /// (todo: use this to implement resizable views).
        ///                                   (18.05.2015.) (Domagoj Saric)
        enum value_type
        {
            default_                   = 0,
            only_reserve_address_space = MAP_NORESERVE
        };
        value_type value;
    };

    static shared_memory create
    (
        access_privileges               ,
        named_object_construction_policy,
        system_hints
    ) noexcept;

    operator mapping() const noexcept
    {
        auto flags{ mapping::create( ap.object_access, viewing::share_mode::shared ) };
        flags.flags |= hints.value;
        return static_cast<mapping &>( flags );
    }

    system_hints                     hints;
    access_privileges                ap;
    named_object_construction_policy nocp;
}; // struct shared_memory

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
