////////////////////////////////////////////////////////////////////////////////
///
/// \file cow_clone.win32.cpp
/// -------------------------
///
/// Windows COW copy constructor for mem_mapping.
/// Duplicates the section handle and maps a PAGE_WRITECOPY view — physical
/// pages are shared until either side writes, at which point the kernel
/// creates private copies.
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include <psi/vm/containers/vm_vector.hpp>

#include <psi/vm/detail/nt.hpp>
#include <psi/vm/handles/handle.hpp>
#include <psi/vm/mappable_objects/file/handle.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

[[ gnu::cold ]]
mem_mapping::mem_mapping( mem_mapping const & source )
{
    if ( !source.has_attached_storage() )
        return;

    auto const total_mapped{ source.mapped_size() };
    if ( !total_mapped )
        return;

    // Duplicate file handle if source is file-backed (for correct
    // is_file_based() reporting and allocation_type in view mapping)
    file_handle cow_file;
    if ( source.mapping_.is_file_based() )
        cow_file = file_handle{ win32::handle_traits::copy( source.mapping_.file.get() ) };

    // Duplicate the section handle (kernel COW: new view gets PAGE_WRITECOPY).
    handle_traits::native_t const dup_section{ win32::handle_traits::copy( source.mapping_.get() ) };

    flags::viewing const cow_view_flags{ PAGE_WRITECOPY };
    mapping_ = { dup_section, cow_view_flags, source.mapping_.ap, std::move( cow_file ) };

    view_ = extendable_mapped_view::map( mapping_, cow_view_flags, 0, total_mapped ); // fallible_result throws on error
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
