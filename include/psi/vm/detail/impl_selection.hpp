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
#ifndef impl_selection_hpp__05AF14B5_B23B_4CB8_A253_FD2D07B37ECF
#define impl_selection_hpp__05AF14B5_B23B_4CB8_A253_FD2D07B37ECF
#pragma once
//------------------------------------------------------------------------------
#include <boost/config.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

// Implementation note:
//   "Anti-pattern" forward includes to reduce the verbosity of files that
// include this header.
//                                            (26.08.2011.) (Domagoj Saric)
#include <boost/preprocessor/facilities/empty.hpp>
#include <boost/preprocessor/facilities/identity.hpp>
//------------------------------------------------------------------------------
#if !defined( PSI_VM_IMPL )
    #if defined( _WIN32 )
        #define PSI_VM_IMPL() win32
    #elif defined( _WIN32_WINNT )
        #define PSI_VM_IMPL() nt
    #elif defined( BOOST_HAS_UNISTD_H )
        #define PSI_VM_IMPL() posix
        #define PSI_VM_POSIX_INLINE inline
    #else
        #define PSI_VM_IMPL() xsi
    #endif
#endif // !defined( PSI_VM_IMPL )

#ifndef PSI_VM_POSIX_INLINE
    #define PSI_VM_POSIX_INLINE
#endif // PSI_VM_POSIX_INLINE

#define PSI_VM_IMPL_INCLUDE( prefix_path, include ) \
    BOOST_PP_STRINGIZE( prefix_path()PSI_VM_IMPL()include() )
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

inline namespace PSI_VM_IMPL() {}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
#endif // impl_selection_hpp
