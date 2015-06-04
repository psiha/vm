////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping_flags.hpp
/// -----------------------
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
#ifndef flags_hpp__504C3F9E_97C2_4E8C_82C6_881340C5FBA6
#define flags_hpp__504C3F9E_97C2_4E8C_82C6_881340C5FBA6
#pragma once
//------------------------------------------------------------------------------
#include "boost/config.hpp"

#include "boost/mmap/mappable_objects/file/win32/mapping_flags.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl> struct shared_memory_flags;

struct win32;

template <>
struct shared_memory_flags<win32> : file_mapping_flags<win32>
{
    struct system_hints
    {
        enum value_type
        {
            default                    = 0x8000000,
            only_reserve_address_space = 0x4000000
        };
    }; // struct system_hints

    static shared_memory_flags<win32> BOOST_CC_REG create
    (
        flags_t                  combined_handle_access_rights,
        share_mode  ::value_type share_mode,
        system_hints::value_type system_hint
    ) noexcept;
}; // struct shared_memory_flags<win32>

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "flags.inl"
#endif

#endif // flags.hpp
