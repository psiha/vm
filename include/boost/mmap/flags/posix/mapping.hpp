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
#include "boost/mmap/flags/flags.hpp"

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
template <typename Impl> struct viewing;

using flags_t = int;

template <>
struct viewing<posix>
{
    enum struct share_mode
    {
        shared = MAP_SHARED,
        hidden = MAP_PRIVATE
    };

    static viewing<posix> BOOST_CC_REG create
    (
        access_privileges<posix>::object,
        share_mode
    ) noexcept;

    flags_t protection; // PROT_*
    flags_t flags     ; // MAP_*
}; // struct viewing<posix>

template <>
struct mapping<posix> : viewing<posix> {};

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "mapping.inl"
#endif // BOOST_MMAP_HEADER_ONLY

#endif // mapping.hpp
