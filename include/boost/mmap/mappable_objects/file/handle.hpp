////////////////////////////////////////////////////////////////////////////////
///
/// \file file/handle.hpp
/// ---------------------
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
#ifndef handle_hpp__56DDDE10_05C3_4B18_8DC5_89317D689F99
#define handle_hpp__56DDDE10_05C3_4B18_8DC5_89317D689F99
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/handles/handle.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl> class handle;

#ifdef BOOST_MSVC
    #pragma warning( push )
    #pragma warning( disable : 4510 ) // Default constructor was implicitly defined as deleted.
#endif // BOOST_MSVC

template <typename Impl>
struct file_handle : handle<Impl>
{
    using handle<Impl>::handle;

    using reference = typename handle<Impl>::reference;

    operator reference () const noexcept { return reference{ this->get() }; }
}; // struct file_handle

#ifdef BOOST_MSVC
    #pragma warning( pop )
#endif // BOOST_MSVC

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // handle_hpp
