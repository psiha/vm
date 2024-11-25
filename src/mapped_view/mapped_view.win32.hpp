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

#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

enum struct mapping_object_type : std::uint32_t
{
    memory = 0,
    file   = MEM_RESERVE
};

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

//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
#endif // _WIN32
