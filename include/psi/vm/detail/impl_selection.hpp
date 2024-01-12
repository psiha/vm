////////////////////////////////////////////////////////////////////////////////
///
/// \file impl_selection.hpp
/// ------------------------
///
/// Copyright (c) Domagoj Saric 2011 - 2024.
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

#include <boost/preprocessor/stringize.hpp>

// Implementation note:
//   Required for PSI_VM_DIR_IMPL_INCLUDE users.
//                                            (26.08.2011.) (Domagoj Saric)
#include <boost/preprocessor/facilities/identity.hpp>
//------------------------------------------------------------------------------
#if !defined( PSI_VM_IMPL )
#   if defined( _WIN32 )
#       define PSI_VM_IMPL() win32
#   elif defined( _WIN32_WINNT )
#       define PSI_VM_IMPL() nt
#   elif __has_include( <unistd.h> )
#       define PSI_VM_IMPL() posix
#       define PSI_VM_POSIX_INLINE inline
#   else
#       define PSI_VM_IMPL() xsi
#   endif
#endif // !defined( PSI_VM_IMPL )

#ifndef PSI_VM_POSIX_INLINE
#   define PSI_VM_POSIX_INLINE
#endif // PSI_VM_POSIX_INLINE

#define PSI_VM_DIR_IMPL_INCLUDE( include ) \
    BOOST_PP_STRINGIZE( PSI_VM_IMPL()/include() )

#define PSI_VM_DIR_IMPL_PREFIXED_INCLUDE( prefix_path, include ) \
    BOOST_PP_STRINGIZE( prefix_path()/PSI_VM_IMPL()/include() )

#define PSI_VM_IMPL_INCLUDE( include ) \
    BOOST_PP_STRINGIZE( include.PSI_VM_IMPL().hpp )
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

inline namespace PSI_VM_IMPL() {}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
