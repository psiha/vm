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
#include "../handle_ref.hpp"
#include "../../implementations.hpp"

#include "boost/config.hpp"
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
class handle<posix>
#ifdef BOOST_MSVC
    : noncopyable
#endif // BOOST_MSVC
{
public:
    typedef int                         native_handle_t;
    typedef handle_ref< handle<posix> > reference;

    explicit handle<posix>( native_handle_t );
    #ifndef BOOST_MSVC
        handle<posix>( handle<posix> const & );
    #endif // BOOST_MSVC

    ~handle<posix>();

    native_handle_t const & get() const { return handle_; }

    bool operator! () const { return !handle_; }
    operator reference () const { return reference( handle_ ); }

private:
    native_handle_t const handle_;
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
