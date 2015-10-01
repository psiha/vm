////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping.hpp
/// -----------------
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
#ifndef mapping_hpp__35B462C8_DB74_4F25_A9B7_6CAC69D9753B
#define mapping_hpp__BFFC0541_21AC_4A80_A9EE_E0450B6D4D8A
#pragma once
//------------------------------------------------------------------------------
#include "flags.hpp"
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

////////////////////////////////////////////////////////////////////////////////
///
/// \class mapping
///
/// \brief Flags for specifying access modes and usage patterns/hints when
/// creating mapping objects.
///
////////////////////////////////////////////////////////////////////////////////

template <typename Impl>
struct mapping
#ifdef DOXYGEN_ONLY
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
}
#endif // DOXYGEN_ONLY
; // struct mapping

template <typename Impl> struct viewing;

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifndef DOXYGEN_ONLY
#include "boost/mmap/detail/impl_selection.hpp"
#include BOOST_MMAP_IMPL_INCLUDE( BOOST_PP_EMPTY, BOOST_PP_IDENTITY( /mapping.hpp ) )
#endif // DOXYGEN_ONLY

#endif // opening_hpp
