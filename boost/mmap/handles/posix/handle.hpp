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
#ifndef handle_hpp__63113526_C3F1_46DC_850E_D8D8C62031DB
#define handle_hpp__63113526_C3F1_46DC_850E_D8D8C62031DB
#pragma once
//------------------------------------------------------------------------------
#include "boost/config.hpp"

#include "boost/noncopyable.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

class posix_handle
#ifdef BOOST_MSVC
    : noncopyable
#endif // BOOST_MSVC
{
public:
    typedef int handle_t;

    explicit posix_handle( handle_t );
    #ifndef BOOST_MSVC
        posix_handle( posix_handle const & );
    #endif // BOOST_MSVC

    ~posix_handle();

    handle_t const & handle() const { return handle_; }

private:
    handle_t const handle_;
};

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "handle.inl"
#endif

#endif // handle_hpp
