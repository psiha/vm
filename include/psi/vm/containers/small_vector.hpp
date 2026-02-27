////////////////////////////////////////////////////////////////////////////////
/// small_vector<T,N> -- inline (stack) buffer with heap spill.
///
/// Alias over vector<sbo_hybrid<T,N,sz_t,options>>.
/// Layout and growth controlled by small_vector_options (see storage/small.hpp).
///
/// Copyright (c) Domagoj Saric.
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once

#include <psi/vm/containers/storage/small.hpp>
#include <psi/vm/containers/vector.hpp>

//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// small_vector_layout, small_vector_options, sbo_hybrid, resolve_layout
// are defined in storage/small.hpp.

template <typename T, std::uint32_t N, typename sz_t = std::uint32_t, small_vector_options options = {}>
requires( is_trivially_moveable<T> )
using small_vector = vector<sbo_hybrid<T, N, sz_t, options>, options.growth>;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
