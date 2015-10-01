////////////////////////////////////////////////////////////////////////////////
///
/// \file win32/handle.hpp
/// ----------------------
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
#ifndef handle_hpp__1CEA6D65_D5C0_474E_833D_2CE927A1C74D
#define handle_hpp__1CEA6D65_D5C0_474E_833D_2CE927A1C74D
#pragma once
//------------------------------------------------------------------------------
#include "../handle_ref.hpp"
#include "../../implementations.hpp"

#include "boost/assert.hpp"
#include "boost/detail/winapi/handles.hpp"

#ifdef BOOST_MSVC
    #include "../posix/handle.hpp"
    #include "io.h"
#endif // BOOST_MSVC

#include <cstdint>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl> struct handle_traits;

template <>
struct handle_traits<win32>
{
    using native_t = boost::detail::winapi::HANDLE_;

    static native_t constexpr invalid_value = boost::detail::winapi::invalid_handle_value; //...mrmlj...or nullptr eg. for CreateFileMapping

    static BOOST_ATTRIBUTES( BOOST_COLD BOOST_RESTRICTED_FUNCTION_L2 BOOST_EXCEPTIONLESS )
    void BOOST_CC_REG close( native_t const native_handle )
    {
        BOOST_VERIFY
        (
            ( boost::detail::winapi::CloseHandle( native_handle ) != false ) ||
            ( ( native_handle == nullptr ) || ( native_handle == invalid_value ) )
        );
    }

    static BOOST_ATTRIBUTES( BOOST_COLD BOOST_RESTRICTED_FUNCTION_L2 BOOST_EXCEPTIONLESS )
    native_t BOOST_CC_REG copy( native_t const native_handle ); //todo
}; // handle_traits<win32>

#ifdef BOOST_MSVC
inline
handle_traits<posix>::native_t make_posix_handle( handle_traits<win32>::native_t const native_handle, int const flags )
{
    return static_cast<handle_traits<posix>::native_t>( ::_open_osfhandle( reinterpret_cast<std::intptr_t>( native_handle ), flags ) );
}
#endif // BOOST_MSVC

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // handle_hpp
