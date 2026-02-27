////////////////////////////////////////////////////////////////////////////////
///
/// \file cow_clone_impl.hpp
/// ------------------------
///
/// Internal header declaring the platform-specific COW clone helper.
/// The helper produces a new (mapping, view) pair that shares physical pages
/// with the source via kernel COW semantics.
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once

#include <psi/vm/mapped_view/mapped_view.hpp>
#include <psi/vm/mapping/mapping.hpp>

#include <cstddef>
//------------------------------------------------------------------------------
namespace psi::vm::detail
{
//------------------------------------------------------------------------------

struct cow_clone_result
{
    mapping     mapping_;
    mapped_view view_;
    explicit operator bool() const noexcept { return static_cast<bool>( mapping_ ); }
};

// Platform-specific: create a COW view of the source mapping+view.
// Returns a new mapping+view pair sharing physical pages (kernel COW).
//
// For file-backed mappings: dup handle + MAP_PRIVATE / PAGE_WRITECOPY.
// For anonymous: platform-specific (Windows WRITECOPY, macOS mach_vm_remap,
// Linux memfd_create or mremap).
//
// Implemented in cow_clone.win32.cpp and cow_clone.posix.cpp.
// src_view passed as mapped_span (non-owning, trivially copyable by value).
[[ gnu::cold, nodiscard ]]
cow_clone_result cow_clone
(
    mapping     const & src_mapping,
    mapped_span         src_view,
    std::size_t         total_mapped
);

//------------------------------------------------------------------------------
} // namespace psi::vm::detail
//------------------------------------------------------------------------------
