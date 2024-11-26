////////////////////////////////////////////////////////////////////////////////
///
/// \file flags/mapping.win32.hpp
/// -----------------------------
///
/// Copyright (c) Domagoj Saric 2010 - 2024.
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
#pragma once

#include <psi/vm/detail/impl_selection.hpp>
#include <psi/vm/flags/flags.win32.hpp>

#include <boost/winapi/security.hpp>

#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace win32
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

using flags_t = unsigned long; // DWORD


struct [[ clang::trivial_abi ]] viewing
{
    using access_rights = access_privileges;

    enum struct share_mode : std::uint8_t
    {
        shared = 0,
        hidden = 0x08
    };

    bool is_cow   () const noexcept;
    bool is_hidden() const noexcept { return is_cow(); }

    static viewing BOOST_CC_REG create
    (
        access_privileges::object,
        share_mode
    ) noexcept;

    bool operator< ( viewing const other ) const noexcept
    {
        return
            ( ( other.page_protection & PAGE_READWRITE ) && !( this->page_protection & PAGE_READWRITE ) ) ||
            ( ( other.page_protection > PAGE_EXECUTE   ) && !( this->page_protection > PAGE_EXECUTE   ) );
    }

    bool operator<=( viewing const other ) const noexcept
    {
        return ( this->page_protection == other.page_protection ) || ( *this < other );
    }

    flags_t page_protection;
}; // struct viewing

namespace detail
{
    flags_t object_access_to_page_access( access_privileges::object, viewing::share_mode ) noexcept;
} // namespace detail


struct mapping
{
    using access_rights = viewing::access_rights;
    using share_mode    = viewing::share_mode   ;

    static mapping create
    (
        access_rights,
        named_object_construction_policy,
        share_mode
    ) noexcept;

    viewing map_view_flags() const noexcept { return { page_protection }; }

    flags_t                          page_protection;
    access_rights                    ap; // access_privileges::object part required for file-based named_memory
    named_object_construction_policy creation_disposition;
}; // struct mapping

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
