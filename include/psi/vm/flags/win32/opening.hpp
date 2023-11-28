////////////////////////////////////////////////////////////////////////////////
///
/// \file open_flags.hpp
/// --------------------
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
#ifndef opening_hpp__E8413961_69B7_4F59_8011_CB65D5EDF6F4
#define opening_hpp__E8413961_69B7_4F59_8011_CB65D5EDF6F4
#pragma once
//------------------------------------------------------------------------------
#include "psi/vm/detail/impl_selection.hpp"
#include "psi/vm/flags/win32/flags.hpp"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winline-namespace-reopened-noninline"
#endif
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------
namespace win32
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

using flags_t = unsigned long; // DWORD


struct access_pattern_optimisation_hints // flags_and_attributes
{
    enum value_type
    {
        generic           = 0,
        random_access     = 0x10000000,
        sequential_access = 0x08000000,
        avoid_caching     = 0x20000000 | 0x80000000,
        temporary         = 0x00000100 | 0x04000000
    };
}; // struct access_pattern_optimisation_hints
using system_hints = access_pattern_optimisation_hints;


struct opening
{
    static opening BOOST_CC_REG create
    (
        access_privileges                const &       ap,
        named_object_construction_policy         const construction_policy,
        flags_t                                  const system_hints
    )
    {
        return { ap, construction_policy, system_hints };
    }

    static opening BOOST_CC_REG create_for_opening_existing_objects
    (
        access_privileges::object,
        access_privileges::child_process,
        flags_t system_hints,
        bool truncate
    );

    access_privileges                ap; //desired_access; // flProtect object child_process system
    named_object_construction_policy creation_disposition;
    flags_t                          flags_and_attributes; // access_pattern_optimisation_hints
}; // struct opening

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------

#ifdef PSI_VM_HEADER_ONLY
    #include "opening.inl"
#endif // PSI_VM_HEADER_ONLY

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif // opening_hpp
