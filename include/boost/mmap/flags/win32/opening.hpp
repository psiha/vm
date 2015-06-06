////////////////////////////////////////////////////////////////////////////////
///
/// \file open_flags.hpp
/// --------------------
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
#ifndef opening_hpp__E8413961_69B7_4F59_8011_CB65D5EDF6F4
#define opening_hpp__E8413961_69B7_4F59_8011_CB65D5EDF6F4
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/implementations.hpp"

#include <cstdint>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

template <typename Impl> struct opening;

using flags_t = std::uint32_t;

template <>
struct opening<win32>
{
    enum struct system_object_construction_policy // creation disposition
    {
        create_new                      = 1,
        create_new_or_truncate_existing = 2,
        open_existing                   = 3,
        open_or_create                  = 4,
        open_and_truncate_existing      = 5
    };

    struct new_system_object_public_access_rights
    {
        enum flags
        {
            read    = 0x00000001,
            write   = 0x00000080,
            execute = 0x00000001,
        };
    };

    struct process_private_access_rights
    {
        enum flags
        {
            metaread  = 0,
            read      = 0x80000000L,
            write     = 0x40000000L,
            readwrite = read | write,
            all       = 0x10000000L
        };
    };
    using access_rights = process_private_access_rights;

    struct access_pattern_optimisation_hints
    {
        enum flags
        {
            random_access     = 0x10000000,
            sequential_access = 0x08000000,
            avoid_caching     = 0x20000000 | 0x80000000,
            temporary         = 0x00000100 | 0x04000000
        };
    };
    using system_hints = access_pattern_optimisation_hints;

    static opening<win32> BOOST_CC_REG create
    (
        flags_t handle_access_flags      ,
        system_object_construction_policy,
        flags_t system_hints             ,
        flags_t on_construction_rights
    );

    static opening<win32> BOOST_CC_REG create_for_opening_existing_files
    (
        flags_t handle_access_flags,
        flags_t system_hints       ,
        bool    truncate
    );

    flags_t                           desired_access      ;
    system_object_construction_policy creation_disposition;
    flags_t                           flags_and_attributes;
}; // struct opening<win32>

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "opening.inl"
#endif // BOOST_MMAP_HEADER_ONLY

#endif // opening_hpp
