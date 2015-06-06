////////////////////////////////////////////////////////////////////////////////
///
/// \file flags/win32/mapping.hpp
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
#ifndef mapping_hpp__4EF4F246_E244_40F1_A1C0_6D91EF1DA2EC
#define mapping_hpp__4EF4F246_E244_40F1_A1C0_6D91EF1DA2EC
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/implementations.hpp"

#include "boost/detail/winapi/security.hpp"

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

template <typename Impl> struct mapping;

using flags_t = std::uint32_t;

template <>
struct mapping<win32>
{
    struct access_rights
    {
        enum flags
        {
            read    = 0x0004,
            write   = 0x0002,
            execute = 0x0020,
            all     = read | write | execute
        };
    };

    enum struct share_mode
    {
        shared = 0,
        hidden = 0x0001
    };

    static mapping<win32> BOOST_CC_REG create
    (
        flags_t    combined_handle_access_rights,
        share_mode
    ) noexcept;

    flags_t create_mapping_flags;
    flags_t map_view_flags      ;
    /*...mrmlj...boost::detail::winapi::SECURITY_ATTRIBUTES_*/
    void const * p_security_attributes /*= nullptr...mrmlj...otherwise MSVC14RC barfs at brace-initialisation*/;
}; // struct mapping<win32>

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
