////////////////////////////////////////////////////////////////////////////////
///
/// \file expand_linux.hpp
/// ----------------------
///
/// Shared Linux expand primitive: mremap wrapper.
///
/// Copyright (c) Domagoj Saric 2023 - 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once
#ifdef __linux__
#include <sys/mman.h>

#include <psi/vm/allocation.hpp> // reallocation_type

#include <boost/assert.hpp>

#include <cerrno>
#include <cstddef>
//------------------------------------------------------------------------------
namespace psi::vm::detail
{
//------------------------------------------------------------------------------

struct mremap_result
{
    void * address;
    explicit operator bool() const noexcept { return address != MAP_FAILED; }
};

/// Linux mremap: atomic in-place or relocating expansion.
/// Preserves MAP_PRIVATE (COW) pages transparently.
///
/// \param realloc_type  moveable → MREMAP_MAYMOVE (may relocate);
///                      fixed    → in-place only (fails if not possible)
[[ nodiscard, gnu::cold ]]
inline mremap_result linux_mremap(
    void            * const address,
    std::size_t       const current_size,
    std::size_t       const target_size,
    reallocation_type const realloc_type = reallocation_type::moveable
) noexcept
{
    auto const result{ ::mremap( address, current_size, target_size, static_cast<int>( realloc_type ) ) };
    if ( result == MAP_FAILED )
    {
        BOOST_ASSERT_MSG( ( errno == ENOMEM ) && ( realloc_type == reallocation_type::fixed ), "Unexpected mremap failure" );
    }
    return { result };
}

//------------------------------------------------------------------------------
} // namespace psi::vm::detail
//------------------------------------------------------------------------------
#endif // __linux__
