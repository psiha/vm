////////////////////////////////////////////////////////////////////////////////
///
/// \file error.hpp
/// ---------------
///
/// Copyright (c) Domagoj Saric 2015 - 2018.
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
#ifndef error_hpp__6EA873DA_5571_444D_AA8C_AAB9874C529D
#define error_hpp__6EA873DA_5571_444D_AA8C_AAB9874C529D
#pragma once
//------------------------------------------------------------------------------
#include "psi/vm/detail/impl_selection.hpp"

#include PSI_VM_IMPL_INCLUDE( BOOST_PP_EMPTY, BOOST_PP_IDENTITY( /error.hpp ) )

#include <psi/err/fallible_result.hpp>
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------

template <typename Result>
using fallible_result = err::fallible_result<Result, error>;

//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------
#endif // error_hpp
