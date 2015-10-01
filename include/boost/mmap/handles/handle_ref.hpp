////////////////////////////////////////////////////////////////////////////////
///
/// \file handle_ref.hpp
/// --------------------
///
/// Copyright (c) Domagoj Saric 2011 - 2015.
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

#ifdef _MSC_VER
    #pragma warning( push )
    #pragma warning( disable : 4510 ) // Default constructor was implicitly defined as deleted.
    #pragma warning( disable : 4512 ) // Assignment operator could not be generated.
#endif // _MSC_VER

template <typename Handle>
struct handle_ref
{
    using native_handle_t = typename Handle::native_handle_t;

             native_handle_t const & get() const noexcept { return value; }
    operator native_handle_t const &    () const noexcept { return value; }

    native_handle_t const value;
}; // struct handle_ref

#ifdef _MSC_VER
    #pragma warning( pop )
#endif // _MSC_VER

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // handle_ref_hpp
