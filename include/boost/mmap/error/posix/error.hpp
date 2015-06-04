////////////////////////////////////////////////////////////////////////////////
///
/// \file posix/error.hpp
/// ---------------------
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
#ifndef error_hpp__F043103F_57C5_4EFA_A947_15EE812CF090
#define error_hpp__F043103F_57C5_4EFA_A947_15EE812CF090
#pragma once
//------------------------------------------------------------------------------
#include "boost/err/errno.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl> struct error;

struct posix;

template <> struct error<posix> : err::last_errno {};

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // errno_hpp
