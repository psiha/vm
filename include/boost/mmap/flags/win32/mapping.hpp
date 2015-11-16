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
#include "boost/mmap/detail/impl_selection.hpp"
#include "boost/mmap/flags/win32/flags.hpp"

#include <boost/detail/winapi/security.hpp>

#include <cstdint>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
namespace win32
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

using flags_t = unsigned long; // DWORD


struct viewing
{
    using access_rights = access_privileges;    

    enum struct share_mode
    {
        shared = 0,
        hidden = 0x0001
    };

    bool is_cow() const;
    bool is_hidden() const { return is_cow(); }

    static viewing BOOST_CC_REG create
    (
        access_privileges::object,
        share_mode
    ) noexcept;

    bool operator< ( viewing const other ) const noexcept
    {
        return
            ( ( other.map_view_flags & access_privileges::write   ) && !( this->map_view_flags & access_privileges::write   ) ) ||
            ( ( other.map_view_flags & access_privileges::execute ) && !( this->map_view_flags & access_privileges::execute ) );
    }

    bool operator<=( viewing const other ) const noexcept
    {
        return ( this->map_view_flags == other.map_view_flags ) || ( *this < other );
    }

    flags_t map_view_flags;
}; // struct viewing

namespace detail
{
    flags_t BOOST_CC_REG object_access_to_page_access( access_privileges::object, viewing::share_mode );
} // namespace detail


struct mapping
{
    using access_rights = viewing::access_rights;
    using share_mode    = viewing::share_mode   ;

    static mapping BOOST_CC_REG create
    (
        access_privileges,
        named_object_construction_policy,
        share_mode
    ) noexcept;

  //access_privileges                ap; //desired_access      ; // flProtect object child_process system
    flags_t                          create_mapping_flags;
    access_privileges::object        object_access       ; // ...mrmlj...for file-based named_memory
    access_privileges::child_process child_access        ;
    access_privileges::system        system_access       ;
    named_object_construction_policy creation_disposition;
    viewing                          map_view_flags      ;
}; // struct mapping

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "mapping.inl"
#endif // BOOST_MMAP_HEADER_ONLY

#endif // mapping.hpp
