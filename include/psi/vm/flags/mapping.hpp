////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping.hpp
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
#pragma once

//------------------------------------------------------------------------------
#include "flags.hpp"
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
#ifdef DOXYGEN_ONLY
namespace flags
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///
/// \class mapping
///
/// \brief Flags for specifying access modes and usage patterns/hints when
/// creating mapping objects.
///
////////////////////////////////////////////////////////////////////////////////


struct mapping
{
    enum struct share_mode
    {
        shared, ///< Enable IPC access to the mapped region
        hidden  ///< Map as process-private (i.e. w/ COW semantics)
    };

    static mapping create ///< Factory function
    (
        flags_t    combined_handle_access_rights,
        share_mode share_mode
    );

    unspecified-impl_specific public_data_members;
}; // struct mapping

struct viewing;

//------------------------------------------------------------------------------
} // namespace flags
#endif // DOXYGEN_ONLY
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------

#ifndef DOXYGEN_ONLY
#include <psi/vm/detail/impl_selection.hpp>
#include PSI_VM_IMPL_INCLUDE( mapping )
#endif // DOXYGEN_ONLY
