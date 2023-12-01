////////////////////////////////////////////////////////////////////////////////
///
/// \file flags/posix/mapping.hpp
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
#ifndef mapping_hpp__79CF82B8_F71B_4C75_BE77_98F4FB8A7FFA
#define mapping_hpp__79CF82B8_F71B_4C75_BE77_98F4FB8A7FFA
#pragma once
//------------------------------------------------------------------------------
#include "psi/vm/detail/impl_selection.hpp"
#include "psi/vm/detail/posix.hpp"
#include "psi/vm/flags/flags.hpp"

#include "sys/mman.h"
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------
PSI_VM_POSIX_INLINE
namespace posix
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

using flags_t = int;

struct viewing
{
    enum struct share_mode
    {
        shared = MAP_SHARED,
        hidden = MAP_PRIVATE
    };

    static viewing BOOST_CC_REG create
    (
        access_privileges::object,
        share_mode
    ) noexcept;

    bool operator< ( viewing const other ) const noexcept
    {
        return
            ( ( static_cast< std::uint32_t >( other.protection ) & access_privileges::write   ) && !( static_cast< std::uint32_t >( this->protection ) & access_privileges::write   ) ) ||
            ( ( static_cast< std::uint32_t >( other.protection ) & access_privileges::execute ) && !( static_cast< std::uint32_t >( this->protection ) & access_privileges::execute ) );
    }

    bool operator<=( viewing const other ) const noexcept
    {
        return ( this->protection == other.protection ) || ( *this < other );
    }

    flags_t protection; // PROT_*
    flags_t flags     ; // MAP_*
}; // struct viewing

using mapping = viewing;

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------

#ifdef PSI_VM_HEADER_ONLY
    #include "mapping.inl"
#endif // PSI_VM_HEADER_ONLY

#endif // mapping.hpp
