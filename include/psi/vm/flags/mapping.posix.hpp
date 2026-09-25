////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2010 - 2026.
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

    //! A private (MAP_PRIVATE) view: writes through it land in private page
    //! copies that no other view of the same object ever sees. (Masked with
    //! both type bits: Linux's MAP_SHARED_VALIDATE is MAP_SHARED | MAP_PRIVATE.)
    bool is_cow   () const noexcept { return ( flags & ( MAP_SHARED | MAP_PRIVATE ) ) == MAP_PRIVATE; }
    bool is_hidden() const noexcept { return is_cow(); }

    bool operator< ( viewing const other ) const noexcept
    {
        return
            ( ( static_cast<std::uint32_t>( other.protection ) & access_privileges::write   ) && !( static_cast<std::uint32_t>( this->protection ) & access_privileges::write   ) ) ||
            ( ( static_cast<std::uint32_t>( other.protection ) & access_privileges::execute ) && !( static_cast<std::uint32_t>( this->protection ) & access_privileges::execute ) );
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
