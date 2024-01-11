////////////////////////////////////////////////////////////////////////////////
///
/// \file handle.hpp
/// ----------------
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

#include <psi/vm/handles/handle.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>

#ifdef _MSC_VER
#   pragma warning ( disable : 4996 ) // "The POSIX name for this item is deprecated. Instead, use the ISO C++ conformant name."
#   include <io.h>
#else
#   include <unistd.h>
#endif
#include <fcntl.h>

#include <cerrno>
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

struct handle_traits
{
    using native_t = int;

    static native_t constexpr invalid_value = -1;

    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_RESTRICTED_FUNCTION_L1, BOOST_EXCEPTIONLESS )
    void close( native_t const native_handle ) noexcept
    {
        BOOST_VERIFY
        (
            ( ::close( native_handle ) == 0 ) ||
            (
                ( native_handle == invalid_value ) &&
                ( errno         == EBADF         )
            )
        );
    }

    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_RESTRICTED_FUNCTION_L1, BOOST_EXCEPTIONLESS )
    native_t copy( native_t native_handle ); // TODO
}; // handle_traits

using handle = handle_impl<handle_traits>;

//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------
