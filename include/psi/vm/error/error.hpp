////////////////////////////////////////////////////////////////////////////////
///
/// \file error.hpp
/// ---------------
///
/// Copyright (c) Domagoj Saric 2015 - 2024.
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

#include <psi/vm/detail/impl_selection.hpp>
#include PSI_VM_IMPL_INCLUDE( error )

#include <psi/err/fallible_result.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

template <typename Result>
using fallible_result = err::fallible_result<Result, error>;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
