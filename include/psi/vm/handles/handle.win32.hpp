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
#pragma once

#include <psi/vm/handles/handle.hpp>
#include <psi/vm/handles/handle_ref.hpp>

#include <boost/assert.hpp>
#include <boost/winapi/handles.hpp>

#include "handle.posix.hpp"
#include <io.h>

#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace win32
{
//------------------------------------------------------------------------------

struct handle_traits
{
    using native_t = boost::winapi::HANDLE_;

    inline static native_t const invalid_value{ nullptr }; // Win32 is inconsistent: some parts use null while other parts use INVALID_HANDLE_VALUE

    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_RESTRICTED_FUNCTION_L1, BOOST_EXCEPTIONLESS )
    void close( native_t const native_handle )
    {
#   if defined( __clang__ ) || defined( __GNUC__ )
        if ( __builtin_constant_p( native_handle ) && ( native_handle == nullptr || native_handle == boost::winapi::INVALID_HANDLE_VALUE_ ) )
            return;
#   endif
        BOOST_VERIFY
        (
            ( boost::winapi::CloseHandle( native_handle ) != false ) ||
            ( ( native_handle == nullptr ) || ( native_handle == invalid_value ) )
        );
    }

    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_RESTRICTED_FUNCTION_L1, BOOST_EXCEPTIONLESS )
    native_t copy( native_t native_handle ); // TODO
}; // handle_traits

#ifdef BOOST_MSVC
inline
posix::handle_traits::native_t get_posix_handle( handle_traits::native_t const native_handle, int const flags )
{
    return static_cast<posix::handle_traits::native_t>( ::_open_osfhandle( reinterpret_cast<std::intptr_t>( native_handle ), flags ) );
}
#endif // BOOST_MSVC

using handle = handle_impl<handle_traits>;

//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
