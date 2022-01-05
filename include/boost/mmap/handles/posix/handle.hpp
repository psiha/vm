////////////////////////////////////////////////////////////////////////////////
///
/// \file handle.hpp
/// ----------------
///
/// Copyright (c) Domagoj Saric 2010 - 2019.
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
#ifndef handle_hpp__63113526_C3F1_46DC_850E_D8D8C62031DB
#define handle_hpp__63113526_C3F1_46DC_850E_D8D8C62031DB
#pragma once
//------------------------------------------------------------------------------
#include "../handle.hpp"
#include "../handle_ref.hpp"

#include <boost/mmap/detail/impl_selection.hpp>

#include <boost/assert.hpp>
#include <boost/config.hpp>

#ifdef _MSC_VER
    #pragma warning ( disable : 4996 ) // "The POSIX name for this item is deprecated. Instead, use the ISO C++ conformant name."
    #include <io.h>
#endif // BOOST_MSVC
#include <fcntl.h>

#include <cerrno>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
BOOST_MMAP_POSIX_INLINE
namespace posix
{
//------------------------------------------------------------------------------

struct handle_traits
{
    using native_t = int;

    static native_t const invalid_value = -1;

    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_RESTRICTED_FUNCTION_L1, BOOST_EXCEPTIONLESS )
    void BOOST_CC_REG close( native_t const native_handle )
    {
        BOOST_VERIFY
        (
            ( ::close( native_handle ) == 0 ) ||
            (
                ( native_handle == -1    ) &&
                ( errno         == EBADF )
            )
        );
    }

    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_RESTRICTED_FUNCTION_L1, BOOST_EXCEPTIONLESS )
    native_t BOOST_CC_REG copy( native_t const native_handle ); //todo
}; // handle_traits

using handle = handle_impl<handle_traits>;

//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // handle_hpp
