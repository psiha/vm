////////////////////////////////////////////////////////////////////////////////
/// Heap-allocated vector
///
/// heap_vector<T> is a type alias for vector<heap_storage<T>>.
/// Key advantage over std::vector: in-place expansion via allocator expand
/// (avoids the double-allocation + element-wise move inherent to std::vector's
/// limited allocator interface). Works with all element types.
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

#include <psi/vm/containers/storage/heap.hpp>
#include <psi/vm/containers/vector.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// heap_vector<T, sz_t, options, growth> = vector<heap_storage<T, sz_t, void, options>, growth>
// The 'void' Allocator default auto-computes to crt_allocator with correct alignment.
// Growth policy lives at the vector<> level, not inside storage.
template <typename T, typename sz_t = std::size_t, heap_options options = {}, geometric_growth growth = {}>
using heap_vector = vector<heap_storage<T, sz_t, void, options>, growth>;

// Generic erase_if / erase are provided in vector.hpp for all psi_vm_vector types.

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
