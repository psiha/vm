////////////////////////////////////////////////////////////////////////////////
///
/// \file posix/flags.inl
/// ---------------------
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
#include <psi/vm/mappable_objects/shared_memory/posix/flags.hpp>
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

shared_memory shared_memory::create
(
    access_privileges                const ap,
    named_object_construction_policy const nocp,
    system_hints                     const system_hint
) noexcept
{
    return { system_hint, ap, nocp };
}

//------------------------------------------------------------------------------
} // flags
//------------------------------------------------------------------------------
} // posix
//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
