////////////////////////////////////////////////////////////////////////////////
///
/// \file error.hpp
/// ---------------
///
/// Copyright (c) Domagoj Saric 2015.
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
#ifndef error_hpp__6EA873DA_5571_444D_AA8C_AAB9874C529D
#define error_hpp__6EA873DA_5571_444D_AA8C_AAB9874C529D
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/detail/impl_selection.hpp"

#include BOOST_MMAP_IMPL_INCLUDE( BOOST_PP_EMPTY, BOOST_PP_IDENTITY( /error.hpp ) )
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl = BOOST_MMAP_IMPL()> struct error;

template <typename Result, typename Impl>
using fallible_result = err::fallible_result<Result, error<Impl>>;

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // error_hpp
