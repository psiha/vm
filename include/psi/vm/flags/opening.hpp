////////////////////////////////////////////////////////////////////////////////
///
/// \file opening.hpp
/// -----------------
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
#ifndef opening_hpp__FEEA10FA_EA28_496E_B860_815D484AFB36
#define opening_hpp__FEEA10FA_EA28_496E_B860_815D484AFB36
#pragma once
//------------------------------------------------------------------------------
#include "flags.hpp"
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

#ifdef DOXYGEN_ONLY
struct opening
{
    /// Access-pattern optimisation hints
    struct access_pattern_optimisation_hints
    {
        enum value_type
        {
            random_access,
            sequential_access,
            avoid_caching,
            temporary
        };
    };
    using system_hints = access_pattern_optimisation_hints;

    /// Factory function
    static opening create
    (
        flags_t handle_access_flags,
        open_policy,
        flags_t system_hints,
        flags_t on_construction_rights
    );

    /// Factory function
    static opening create_for_opening_existing_files
    (
        flags_t handle_access_flags,
        flags_t system_hints
        bool    truncate,
    );

    unspecified-impl_specific public_data_members;
}; // struct opening
#endif // DOXYGEN_ONLY

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------

#ifndef DOXYGEN_ONLY
#include "psi/vm/detail/impl_selection.hpp"
#include PSI_VM_IMPL_INCLUDE( BOOST_PP_EMPTY, BOOST_PP_IDENTITY( /opening.hpp ) )
#endif // DOXYGEN_ONLY

#endif // opening_hpp
