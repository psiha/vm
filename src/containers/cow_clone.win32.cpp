////////////////////////////////////////////////////////////////////////////////
///
/// \file cow_clone.win32.cpp
/// -------------------------
///
/// Windows COW clone implementation: PAGE_WRITECOPY view of the same section.
/// Works for both file-backed and anonymous (pagefile) sections.
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "cow_clone_impl.hpp"

#include <psi/vm/detail/nt.hpp>
#include <psi/vm/handles/handle.hpp>
#include <psi/vm/mappable_objects/file/handle.hpp>
//------------------------------------------------------------------------------
namespace psi::vm::detail
{
//------------------------------------------------------------------------------

[[ gnu::cold ]]
cow_clone_result
cow_clone
(
    mapping     const & src_mapping,
    mapped_span         /*src_view*/,
    std::size_t const   total_mapped
)
{
    // Duplicate the section handle (kernel COW: new view gets PAGE_WRITECOPY)
    auto const dup_section{ win32::handle_traits::copy( src_mapping.get() ) };
    if ( !dup_section ) [[ unlikely ]]
        return {};

    // Duplicate file handle if source is file-backed (for correct
    // is_file_based() reporting and allocation_type in view mapping)
    file_handle cow_file;
    if ( src_mapping.is_file_based() )
        cow_file = file_handle{ win32::handle_traits::copy( src_mapping.file.get() ) };

    flags::viewing cow_view_flags{ PAGE_WRITECOPY };
    mapping cow_mapping{ dup_section, cow_view_flags, src_mapping.ap, std::move( cow_file ) };

    auto cow_view{ mapped_view::map( cow_mapping, cow_view_flags, 0, total_mapped ).as_result_or_error() };
    if ( !cow_view ) [[ unlikely ]]
        return {}; // cow_mapping destructor closes dup_section

    return { std::move( cow_mapping ), *std::move( cow_view ) };
}

//------------------------------------------------------------------------------
} // namespace psi::vm::detail
//------------------------------------------------------------------------------
