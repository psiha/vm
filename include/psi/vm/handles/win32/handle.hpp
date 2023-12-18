////////////////////////////////////////////////////////////////////////////////
///
/// \file win32/handle.hpp
/// ----------------------
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
#ifndef handle_hpp__1CEA6D65_D5C0_474E_833D_2CE927A1C74D
#define handle_hpp__1CEA6D65_D5C0_474E_833D_2CE927A1C74D
#pragma once
//------------------------------------------------------------------------------
#include "../handle.hpp"
#include "../handle_ref.hpp"

#include <psi/vm/detail/impl_selection.hpp>

#include <boost/assert.hpp>
#include <boost/winapi/handles.hpp>

#include "../posix/handle.hpp"
#include <io.h>

#include <cstdint>

//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------
inline namespace win32
{
//------------------------------------------------------------------------------

struct handle_traits
{
    using native_t = boost::winapi::HANDLE_;

    inline static native_t const invalid_value = boost::winapi::invalid_handle_value; //...mrmlj...or nullptr eg. for CreateFileMapping

    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_RESTRICTED_FUNCTION_L2, BOOST_EXCEPTIONLESS )
    void BOOST_CC_REG close( native_t const native_handle )
    {
        BOOST_VERIFY
        (
            ( boost::winapi::CloseHandle( native_handle ) != false ) ||
            ( ( native_handle == nullptr ) || ( native_handle == invalid_value ) )
        );
    }

    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_RESTRICTED_FUNCTION_L2, BOOST_EXCEPTIONLESS )
    native_t BOOST_CC_REG copy( native_t const native_handle ); //todo
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
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------
#endif // handle_hpp
