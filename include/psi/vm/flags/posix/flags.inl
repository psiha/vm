////////////////////////////////////////////////////////////////////////////////
///
/// \file flags/posix/flags.inl
/// ---------------------------
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
#ifndef flags_inl__8658DC6F_321D_4ED4_A599_DB5CD7540BC1
#define flags_inl__8658DC6F_321D_4ED4_A599_DB5CD7540BC1
#pragma once
//------------------------------------------------------------------------------
#include "flags.hpp"

#include "psi/vm/detail/impl_inline.hpp"
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------
inline namespace posix
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

BOOST_IMPL_INLINE
flags_t BOOST_CC_REG access_privileges::oflag() const noexcept
{
    //...zzz...use fadvise...
    // http://stackoverflow.com/questions/2299402/how-does-one-do-raw-io-on-mac-os-x-ie-equivalent-to-linuxs-o-direct-flag

    flags_t result( ( object_access.privileges >> procsh ) & 0xFF );

#if O_RDWR != ( O_RDONLY | O_WRONLY )
    auto constexpr o_rdwr( O_RDONLY_ | O_WRONLY );
    if ( ( result & o_rdwr ) == o_rdwr )
        result &= ~o_rdwr | O_RDWR;
    else
    {
        /// \note Remove the "Undetectable combined O_RDONLY" workaround flag.
        ///                                   (09.11.2015.) (Domagoj Saric)
    #if !O_RDONLY
        result &= ~O_RDONLY_;
    #endif // !O_RDONLY
    }
#endif // no O_RDWR GNUC extension

    result |= static_cast<flags_t>( child_access );

    return result;
}

//------------------------------------------------------------------------------
} // flags
//------------------------------------------------------------------------------
} // posix
//------------------------------------------------------------------------------
} // vm
//------------------------------------------------------------------------------
} // psi
//------------------------------------------------------------------------------
#endif // flags_inl
