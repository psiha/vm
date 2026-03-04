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
/// COW copy, etc.) is provided by vm_storage / mem_mapping.
///
/// Uses geometric growth {4,3} (1.33×) for append operations (push_back,
/// emplace_back) to prevent O(n²) mach_vm_remap cost on macOS from
/// one-at-a-time growth. Explicit sizing operations (grow_to, grow_by,
/// resize) bypass the growth policy and use exact-fit — this keeps
/// file-backed storage compact when the caller knows the target size.
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
using vm_vector = vector<vm_storage<T, sz_t>, geometric_growth{ 4, 3 }>;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
