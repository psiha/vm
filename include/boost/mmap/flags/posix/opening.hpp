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
#include "boost/mmap/detail/impl_selection.hpp"
#include "boost/mmap/detail/posix.hpp"
#include "boost/mmap/flags/posix/flags.hpp"

#include "boost/preprocessor/facilities/is_empty.hpp"

#include "fcntl.h"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
inline namespace posix
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

struct access_pattern_optimisation_hints
{
    //...mrmlj...clean/simplify this...
    // https://ext4.wiki.kernel.org/index.php/Clarifying_Direct_IO's_Semantics
    // https://www.reddit.com/r/linux/comments/1j7fxn/thanks_linus_for_keeping_o_direct
    // http://stackoverflow.com/questions/5055859/how-are-the-o-sync-and-o-direct-flags-in-open2-different-alike
#if defined( O_TMPFILE )
    static flags_t constexpr o_tmpfile = O_TMPFILE;
#elif defined( O_TEMPORARY )
    static flags_t constexpr o_tmpfile = O_TEMPORARY | _O_SHORT_LIVED;
#else
    static flags_t constexpr o_tmpfile = 0;
#endif // O_TMPFILE
    enum value_type
    {
        //O_SYNC
        random_access     = BOOST_MMAP_POSIX_STANDARD_LINUX_OSX_MSVC( 0,         0, 0, O_RANDOM     ),
        sequential_access = BOOST_MMAP_POSIX_STANDARD_LINUX_OSX_MSVC( 0,         0, 0, O_SEQUENTIAL ),
        avoid_caching     = BOOST_MMAP_POSIX_STANDARD_LINUX_OSX_MSVC( 0, O_DIRECT , 0, 0            ),
        temporary         = o_tmpfile
    };
}; // struct access_pattern_optimisation_hints
using system_hints = access_pattern_optimisation_hints;

struct opening
{
    static opening BOOST_CC_REG create
    (
        access_privileges,
        named_object_construction_policy,
        flags_t combined_system_hints
    ) noexcept;

    static opening BOOST_CC_REG create_for_opening_existing_objects
    (
        access_privileges::object,
        access_privileges::child_process,
        flags_t combined_system_hints,
        bool truncate
    ) noexcept;

    flags_t oflag;
    flags_t pmode;
}; // struct opening

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "opening.inl"
#endif // BOOST_MMAP_HEADER_ONLY

#endif // opening_hpp
