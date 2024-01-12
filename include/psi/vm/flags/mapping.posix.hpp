////////////////////////////////////////////////////////////////////////////////
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
#include <psi/vm/detail/posix.hpp>
#include <psi/vm/flags/flags.hpp>

#include "sys/mman.h"
//------------------------------------------------------------------------------
namespace psi::vm
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

struct [[ clang::trivial_abi ]] viewing
{
    enum struct share_mode
    {
#   if defined( MAP_SHARED_VALIDATE ) && !defined( NDEBUG )
        shared = MAP_SHARED_VALIDATE,
#   else
        shared = MAP_SHARED,
#   endif
        hidden = MAP_PRIVATE
    };

    static viewing create
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
} // namespace psi::vm
//------------------------------------------------------------------------------
