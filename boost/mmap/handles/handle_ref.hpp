////////////////////////////////////////////////////////////////////////////////
///
/// \file handle_ref.hpp
/// --------------------
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
#ifndef handle_ref_hpp__19A59763_A268_458C_932F_4E42DEA27751
#define handle_ref_hpp__19A59763_A268_458C_932F_4E42DEA27751
#pragma once
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Handle>
struct handle_ref
{
    typedef typename Handle::native_handle_t native_handle_t;

    handle_ref( native_handle_t const value_param ) : value( value_param ) {}

    operator native_handle_t const & () const { return value; }

    native_handle_t const value;
};

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // handle_ref_hpp
