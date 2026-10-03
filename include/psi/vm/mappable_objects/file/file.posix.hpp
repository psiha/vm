////////////////////////////////////////////////////////////////////////////////
///
/// \file mappable_objects/file/file.posix.hpp
/// ------------------------------------------
///
/// Copyright (c) Domagoj Saric 2010 - 2024.
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
#include <psi/vm/detail/posix.hpp>
#include <psi/vm/error/error.posix.hpp>
#include <psi/vm/flags/opening.posix.hpp>
#include <psi/vm/mappable_objects/file/handle.hpp>

#if __has_include( <unistd.h> )
#include <psi/vm/mapping/mapping.posix.hpp>
#endif

#include <psi/err/fallible_result.hpp>

#include <cstddef>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
PSI_VM_POSIX_INLINE
namespace posix
{
//------------------------------------------------------------------------------

template <typename> struct is_resizable;
#if __has_include( <unistd.h> )
template <> struct is_resizable<handle> : std::true_type  {};
#else
template <> struct is_resizable<handle> : std::false_type {};
#endif // POSIX impl level


file_handle create_file( char    const * file_name, flags::opening ) noexcept;
#ifdef _MSC_VER
file_handle create_file( wchar_t const * file_name, flags::opening ) noexcept;
#endif // _MSC_VER

bool delete_file( char    const * path ) noexcept;
bool delete_file( wchar_t const * path ) noexcept;


#if __has_include( <unistd.h> )
err::fallible_result<void, error> set_size( file_handle::reference      , std::uint64_t desired_size ) noexcept;
#endif // POSIX impl level
std::uint64_t                     get_size( file_handle::const_reference                             ) noexcept;

// Extending a file with set_size() already leaves a hole wherever the
// filesystem supports them, so there is nothing to mark (see the Win32
// counterpart).
inline bool make_sparse( file_handle::reference ) noexcept { return true; }

// Deallocates the byte range [offset, offset + length) of the file: it reads
// back as zeros, occupies no disk space (nor page cache) and the file size is
// unchanged, so it can be used to give back the backing store of data that is
// no longer needed while the file stays mapped and addressable. The range is
// clamped to the current file size.
//
// Whole filesystem blocks inside the range are freed, partial blocks at its
// edges are zeroed (they stay allocated). Mapped views of the file observe the
// zeros afterwards.
//
// Returns false where the filesystem cannot do it (the file is then
// unchanged); never throws.
//
// Platform notes:
// - Linux: fallocate( FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE ), supported
//   by ext4, xfs, btrfs, tmpfs...; offset and length need no alignment.
//   MAP_SHARED views stay coherent (the kernel drops the affected cached
//   pages).
// - macOS: fcntl( F_PUNCHHOLE ) (APFS), which requires offset and length to be
//   multiples of the filesystem block size (st_blksize). The unaligned edges
//   of the range are zeroed with ordinary writes and only the aligned interior
//   is punched, so the same semantics hold as elsewhere. Mapped views go
//   through the unified buffer cache.
// - other POSIX systems: not supported, returns false.
bool punch_hole( file_handle::reference, std::uint64_t offset, std::uint64_t length ) noexcept;


#if __has_include( <unistd.h> )
mapping create_mapping
(
    handle                                  && file,
    flags::access_privileges::object           object_access,
    flags::access_privileges::child_process    child_access,
    flags::mapping          ::share_mode       share_mode,
    std::size_t                                size
) noexcept;
#endif // POSIX impl level

//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
