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

#include <psi/vm/containers/storage/vm.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

/// vm_vector<T, sz_t> — persistent/memory-mapped vector.
///
/// An alias for vector<vm_storage<T, sz_t>>. All element-level operations
/// are inherited from vector<>; storage management (map_file, map_memory,
/// COW copy, etc.) is provided by vm_storage / contiguous_storage.
///
/// Uses exact-fit growth {1,1} instead of the default geometric {3,2}
/// because file-backed storage allocates the full capacity to the file via
/// set_size().  Geometric growth on vm_vectors inflates file size by up to
/// 50% — unacceptable for persistent data.  Growth via mremap/remap does
/// not require data copies so the amortization argument for geometric growth
/// does not apply; only the per-expansion syscall cost matters, which is
/// mitigated by page-size rounding in reserve().
///
/// Initialization pattern:
///   vm_vector<float> v;           // default-constructed, no backing store
///   v.map_memory( 1024 );         // attach RAM-backed storage for 1024 floats
///   // or: v.map_file( "data.vec", open_or_create, {} );
///   v.push_back( 3.14f );         // standard container operations
///
/// COW copy:
///   auto clone = v;               // copy constructor: COW clone of mapping
template <typename T, typename sz_t = std::size_t>
using vm_vector = vector<vm_storage<T, sz_t>, geometric_growth{ 1, 1 }>;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
