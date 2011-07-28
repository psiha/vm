////////////////////////////////////////////////////////////////////////////////
///
/// \file flags.hpp
/// ---------------
///
/// Copyright (c) Domagoj Saric 2010.-2011.
///
///  Use, modification and distribution is subject to the Boost Software License, Version 1.0.
///  (See accompanying file LICENSE_1_0.txt or copy at
///  http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef flags_hpp__BFFC0541_21AC_4A80_A9EE_E0450B6D4D8A
#define flags_hpp__BFFC0541_21AC_4A80_A9EE_E0450B6D4D8A
#pragma once
//------------------------------------------------------------------------------
#ifdef _WIN32
#include "../win32_file/flags.hpp"
#else
#include "../posix_file/flags.hpp"
#endif
//------------------------------------------------------------------------------
#endif // flags_hpp
