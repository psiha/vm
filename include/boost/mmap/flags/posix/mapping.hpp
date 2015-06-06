////////////////////////////////////////////////////////////////////////////////
///
/// \file flags/posix/mapping.hpp
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
#ifndef mapping_hpp__79CF82B8_F71B_4C75_BE77_98F4FB8A7FFA
#define mapping_hpp__79CF82B8_F71B_4C75_BE77_98F4FB8A7FFA
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/detail/posix.hpp"
#include "boost/mmap/implementations.hpp"

#include "sys/mman.h"
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

template <typename Impl> struct mapping;

using flags_t = int;

template <>
struct mapping<posix>
{
    struct access_rights
    {
        enum values
        {
            read    = PROT_READ ,
            write   = PROT_WRITE,
            execute = PROT_EXEC ,
            all     = read | write | execute
        };
    };

    enum struct share_mode
    {
        shared = MAP_SHARED,
        hidden = MAP_PRIVATE
    };

    static mapping<posix> BOOST_CC_REG create
    (
        flags_t    combined_handle_access_rights,
        share_mode
    ) noexcept;

    flags_t protection;
    flags_t flags     ;
}; // struct mapping<posix>

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "mapping.inl"
#endif

#endif // mapping.hpp
