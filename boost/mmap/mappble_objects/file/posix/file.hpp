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
#ifndef file_hpp__1E2F9841_1C6C_40D9_9AA7_BAC0003CD909
#define file_hpp__1E2F9841_1C6C_40D9_9AA7_BAC0003CD909
#pragma once
//------------------------------------------------------------------------------
#include "../../../implementations.hpp"
#include "../../../handles/posix/handle.hpp"

#include <cstddef>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl  > struct file_flags;
template <class    Handle> struct is_resizable;

#ifdef BOOST_HAS_UNISTD_H
    template <> struct is_resizable< handle<posix> > : mpl::true_  {};
#else
    template <> struct is_resizable< handle<posix> > : mpl::false_ {};
#endif // BOOST_HAS_UNISTD_H


handle<posix> create_file( char    const * file_name, file_flags<posix> const & );
#ifdef BOOST_MSVC
handle<posix> create_file( wchar_t const * file_name, file_flags<posix> const & );
#endif // BOOST_MSVC

bool delete_file( char    const * file_name, posix );
bool delete_file( wchar_t const * file_name, posix );


#ifdef BOOST_HAS_UNISTD_H
bool        set_size( handle<posix>::reference, std::size_t desired_size );
#endif // BOOST_HAS_UNISTD_H
std::size_t get_size( handle<posix>::reference                           );

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "file.inl"
#endif

#endif // file_hpp
