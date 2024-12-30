////////////////////////////////////////////////////////////////////////////////
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
#ifdef _WIN32
#include <psi/vm/detail/win32.hpp>
#include <psi/vm/flags/mapping.win32.hpp>
#include <psi/vm/mapping/mapping.win32.hpp>
#include <psi/vm/span.hpp>

#include <psi/build/disable_warnings.hpp>

#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// in order for the resizeability/extending of file-backed mappings we have to
// introduce this (runtime) discriminator (idiom for files is reserved mappings
// _and_ views w/ NtExtendSection automatically doing the committing - this also
// helps/solves handling view extensions, specifically committing of the slack
// space in the 64kB allocation/reservation-granularity chunks - while for
// memory it is the more standard explicit mapping-and-committing at the same
// time logic)
enum struct mapping_object_type : std::uint32_t
{
    memory = 0,
    file   = MEM_RESERVE
};

PSI_WARNING_DISABLE_PUSH()
PSI_WARNING_MSVC_DISABLE( 5030 ) // unrecognized attribute

[[ gnu::nothrow, gnu::sysv_abi ]]
mapped_span
windows_mmap
(
    mapping::handle   source_mapping  ,
    void *            desired_position,
    std    ::size_t   desired_size    ,
    std    ::uint64_t offset          ,
    flags  ::viewing  flags           ,
    mapping_object_type
) noexcept;

PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
#endif // _WIN32
