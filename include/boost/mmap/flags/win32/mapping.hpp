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
#include "boost/mmap/flags/win32/flags.hpp"

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

using flags_t = unsigned long; // DWORD

template <>
struct viewing<win32>
{
    using access_rights = access_privileges<win32>;

    enum struct share_mode
    {
        shared = 0,
        hidden = 0x0001
    };

    bool is_cow() const;
    bool is_hidden() const { return is_cow(); }

    static viewing<win32> BOOST_CC_REG create
    (
        access_privileges<win32>::object,
        share_mode
    ) noexcept;

    bool operator< ( viewing<win32> const other ) const noexcept
    {
        return
            ( ( other.map_view_flags & access_rights::write   ) && !( this->map_view_flags & access_rights::write   ) ) ||
            ( ( other.map_view_flags & access_rights::execute ) && !( this->map_view_flags & access_rights::execute ) );
    }

    bool operator<=( viewing<win32> const other ) const noexcept
    {
        return ( this->map_view_flags == other.map_view_flags ) || ( *this < other );
    }

    flags_t map_view_flags;
}; // struct viewing<win32>

namespace detail
{
    flags_t BOOST_CC_REG object_access_to_page_access( access_privileges<win32>::object, viewing<win32>::share_mode );
} // namespace detail

template <>
struct mapping<win32>
{
    using access_rights = viewing<win32>::access_rights;
    using share_mode    = viewing<win32>::share_mode   ;

    static mapping<win32> BOOST_CC_REG create
    (
        access_privileges<win32>,
        named_object_construction_policy<win32>::value_type,
        share_mode
    ) noexcept;

  //access_privileges<win32>                ap; //desired_access      ; // flProtect object child_process system
    flags_t                                 create_mapping_flags;
    access_privileges<win32>::object        object_access       ; // ...mrmlj...for file-based named_memory<win32>
    access_privileges<win32>::child_process child_access        ;
    access_privileges<win32>::system        system_access       ;
    named_object_construction_policy<win32> creation_disposition;
    viewing<win32>                          map_view_flags      ;
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
#endif // BOOST_MMAP_HEADER_ONLY

#endif // mapping.hpp
