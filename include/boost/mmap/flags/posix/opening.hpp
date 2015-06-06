////////////////////////////////////////////////////////////////////////////////
///
/// \file flags/posix/opening.hpp
/// -----------------------------
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
#ifndef opening_hpp__0F422517_D9AA_4E3F_B3E4_B139021D068E
#define opening_hpp__0F422517_D9AA_4E3F_B3E4_B139021D068E
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/detail/posix.hpp"
#include "boost/mmap/implementations.hpp"

#include "fcntl.h"
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

using flags_t = int;

template <>
struct opening<posix>
{
    enum struct system_object_construction_policy
    {
        create_new                      = O_CREAT | O_EXCL ,
        create_new_or_truncate_existing = O_CREAT | O_TRUNC,
        open_existing                   = 0                ,
        open_or_create                  = O_CREAT          ,
        open_and_truncate_existing      = O_TRUNC
    };

    struct new_system_object_public_access_rights
    {
        enum values
        {
            read    = BOOST_MMAP_POSIX_STANDARD_LINUX_OSX_MSVC( S_IRUSR, S_IRUSR, S_IRUSR, _S_IREAD  ),
            write   = BOOST_MMAP_POSIX_STANDARD_LINUX_OSX_MSVC( S_IWUSR, S_IWUSR, S_IWUSR, _S_IWRITE ),
            execute = BOOST_MMAP_POSIX_STANDARD_LINUX_OSX_MSVC( S_IXUSR, S_IXUSR, S_IXUSR, _S_IEXEC  )
        };
    };

    struct process_private_access_rights
    {
        enum flags
        {
            read      = O_RDONLY,
            write     = O_WRONLY,
            readwrite = read | write,
            all       = readwrite
        };
    };
    using access_rights = process_private_access_rights;

    struct access_pattern_optimisation_hints
    {
        enum values
        {
            random_access     = BOOST_MMAP_POSIX_STANDARD_LINUX_OSX_MSVC( 0,        0, 0, O_RANDOM                     ),
            sequential_access = BOOST_MMAP_POSIX_STANDARD_LINUX_OSX_MSVC( 0,        0, 0, O_SEQUENTIAL                 ),
            avoid_caching     = BOOST_MMAP_POSIX_STANDARD_LINUX_OSX_MSVC( 0, O_DIRECT, 0, 0                            ),
            temporary         = BOOST_MMAP_POSIX_STANDARD_LINUX_OSX_MSVC( 0,        0, 0, O_TEMPORARY | _O_SHORT_LIVED ),
        };
    };
    using system_hints = access_pattern_optimisation_hints;

    static opening<posix> BOOST_CC_REG create
    (
        flags_t handle_access_flags      ,
        system_object_construction_policy,
        flags_t system_hints             ,
        flags_t on_construction_rights
    ) noexcept;

    static opening<posix> BOOST_CC_REG create_for_opening_existing_files
    (
        flags_t handle_access_flags,
        flags_t system_hints,
        bool    truncate
    ) noexcept;

    flags_t oflag;
    flags_t pmode;
}; // struct opening<posix>

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
