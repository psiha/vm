////////////////////////////////////////////////////////////////////////////////
/// Fixed capacity vector
///
/// fc_vector<T, N> is now a type alias for vector<fixed_storage<T, N>>.
/// The fixed_storage class (in storage/fixed.hpp) provides the raw memory
/// management; vector<Storage> (in vector.hpp) adds the standard container
/// interface (push_back, iterators, copy/move, etc.) via vector_impl CRTP.
////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric.
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

#include <psi/vm/containers/storage/fixed.hpp>
#include <psi/vm/containers/vector.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// fc_vector<T, N, overflow_handler> = vector<fixed_storage<T, N, overflow_handler>>
template <typename T, std::uint32_t capacity_param, auto overflow_handler = assert_on_overflow{}>
using fc_vector = vector<fixed_storage<T, capacity_param, overflow_handler>>;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
