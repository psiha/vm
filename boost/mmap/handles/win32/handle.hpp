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

#include "../handle_ref.hpp"
#include "../../implementations.hpp"

#include "boost/noncopyable.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl> class handle;

template <>
class handle<win32> : noncopyable
{
public:
    typedef void *                      native_handle_t;
    typedef handle_ref< handle<win32> > reference;
    typedef void * native_handle_t;

    explicit handle<win32>( native_handle_t );
    ~handle<win32>();

    native_handle_t const & get() const { return handle_; }

    bool operator! () const { return !handle_; }
    operator reference () const { return reference( handle_ ); }

private:
    native_handle_t const handle_;
};

#ifdef BOOST_MSVC
    handle<posix> make_posix_handle( handle<win32>::reference, int flags );
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
