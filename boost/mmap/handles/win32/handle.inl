////////////////////////////////////////////////////////////////////////////////
///
/// \file handle.inl
/// ----------------
///
/// Copyright (c) Domagoj Saric 2010.-2011.
///
///  Use, modification and distribution is subject to the Boost Software License, Version 1.0.
///  (See accompanying file LICENSE_1_0.txt or copy at
///  http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "handle.hpp"

#include "../../detail/impl_inline.hpp"

#include "boost/assert.hpp"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#include "windows.h"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

BOOST_IMPL_INLINE
windows_handle::windows_handle( handle_t const handle )
    :
    handle_( handle )
{}

BOOST_IMPL_INLINE
windows_handle::~windows_handle()
{
    BOOST_VERIFY
    (
        ( ::CloseHandle( handle_ ) != false               ) ||
        ( handle_ == 0 || handle_ == INVALID_HANDLE_VALUE )
    );
}


#ifdef BOOST_MSVC
    BOOST_IMPL_INLINE
    posix_handle make_posix_handle( windows_handle::handle_t const native_handle, int const flags )
    {
        return posix_handle( ::_open_osfhandle( reinterpret_cast<intptr_t>( native_handle ), flags ) );
    }
#endif // BOOST_MSVC

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
