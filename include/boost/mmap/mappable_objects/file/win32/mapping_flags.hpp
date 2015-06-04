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
#ifndef mapping_flags_hpp__CD518463_D4CB_4E18_8E35_E0FBBA8CA1D1
#define mapping_flags_hpp__CD518463_D4CB_4E18_8E35_E0FBBA8CA1D1
#pragma once
//------------------------------------------------------------------------------
#include "boost/detail/winapi/security.hpp"

#include <cstdint>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl> struct file_mapping_flags;

struct win32;

using flags_t = std::uint32_t;

template <>
struct file_mapping_flags<win32>
{
    struct handle_access_rights
    {
        enum values
        {
            read    = 0x0004,
            write   = 0x0002,
            execute = 0x0020,
            all     = read | write | execute
        };
    };

    struct share_mode
    {
        enum value_type
        {
            shared = 0,
            hidden = 0x0001
        };
    };

    static file_mapping_flags<win32> BOOST_CC_REG create
    (
        flags_t                combined_handle_access_rights,
        share_mode::value_type share_mode
    );

    flags_t create_mapping_flags;
    flags_t map_view_flags      ;
    /*...mrmlj...boost::detail::winapi::SECURITY_ATTRIBUTES_*/
    void const * p_security_attributes = nullptr;
}; // struct file_mapping_flags<win32>

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "mapping_flags.inl"
#endif

#endif // mapping_flags.hpp
