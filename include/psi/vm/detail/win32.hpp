////////////////////////////////////////////////////////////////////////////////
///
/// \file windows.hpp
/// ------------------
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
#ifndef windows_hpp__886EAB51_B4AD_4246_9BE3_D5272EA7D59F
#define windows_hpp__886EAB51_B4AD_4246_9BE3_D5272EA7D59F
#pragma once
//------------------------------------------------------------------------------
#ifndef PSI_VM_HEADER_ONLY
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif // WIN32_LEAN_AND_MEAN
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif // NOMINMAX
#endif // PSI_VM_HEADER_ONLY
#include "windows.h"
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------


//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------
#endif // windows_hpp
