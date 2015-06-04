////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping.hpp
/// -----------------
///
/// Copyright (c) Domagoj Saric 2010 - 2015.
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
#ifndef mapping_hpp__8B2CEDFB_C87C_4AA4_B9D0_8EF0A42825F2
#define mapping_hpp__8B2CEDFB_C87C_4AA4_B9D0_8EF0A42825F2
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/handles/win32/handle.hpp"
#include "boost/mmap/mappable_objects/file/win32/mapping_flags.hpp"

#include "boost/config.hpp"

#include <cstdint>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl> struct mapping;

template <>
struct mapping<win32>
    :
    handle<win32>
{
    using reference = mapping const &;

    static bool const owns_parent_handle = true;

    mapping( native_handle_t const native_handle, std::uint32_t const view_mapping_flags_param )
        : handle<win32>( native_handle ), view_mapping_flags( view_mapping_flags_param ) {}

    bool is_read_only() const { return ( view_mapping_flags & file_mapping_flags<win32>::handle_access_rights::write ) == 0; }

    std::uint32_t const view_mapping_flags;
}; // struct mapping<win32>

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // mapping_hpp
