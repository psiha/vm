////////////////////////////////////////////////////////////////////////////////
///
/// \file handle.hpp
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
#ifndef handle_hpp__1CEA6D65_D5C0_474E_833D_2CE927A1C74D
#define handle_hpp__1CEA6D65_D5C0_474E_833D_2CE927A1C74D
#pragma once
//------------------------------------------------------------------------------
#ifdef BOOST_MSVC
    #include "../posix/handle.hpp"
#endif

#include "boost/noncopyable.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

class windows_handle : noncopyable
{
public:
    typedef void * handle_t;

    explicit windows_handle( handle_t );
    ~windows_handle();

    handle_t const & handle() const { return handle_; }

private:
    handle_t const handle_;
};

#ifdef BOOST_MSVC
    posix_handle make_posix_handle( windows_handle::handle_t, int const flags );
#endif // BOOST_MSVC

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "handle.inl"
#endif

#endif // handle_hpp
