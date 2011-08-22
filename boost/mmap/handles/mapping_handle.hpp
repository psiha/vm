////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping_handle.hpp
/// ------------------------
///
/// Copyright (c) 2011 Domagoj Saric
///
///  Use, modification and distribution is subject to the Boost Software License, Version 1.0.
///  (See accompanying file LICENSE_1_0.txt or copy at
///  http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef mapping_handle_hpp__D42BC724_FD9A_4C7B_B521_CF3C29C948B3
#define mapping_handle_hpp__D42BC724_FD9A_4C7B_B521_CF3C29C948B3
#pragma once
//------------------------------------------------------------------------------
#include "handle.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl> class handle;

template <typename Impl>
class mapping_handle : handle<Impl>
{
public:
    mapping_handle( typename handle<Impl>::handle_t const native_handle )
        : handle<Impl>( native_handle ) {}
};

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // handle_hpp
