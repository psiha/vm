////////////////////////////////////////////////////////////////////////////////
///
/// \file shared_memory/policies.hpp
/// --------------------------------
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
#ifndef policies_hpp__55090262_271B_4030_9BA1_3FE23F2C9F1E
#define policies_hpp__55090262_271B_4030_9BA1_3FE23F2C9F1E
#pragma once
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

enum struct lifetime_policy
{
    scoped,
    persistent
};

enum struct resizing_policy
{
    fixed,
    resizeable
};


////////////////////////////////////////////////////////////////////////////////
///
/// \class named_memory
///
/// \brief
///
////////////////////////////////////////////////////////////////////////////////

inline namespace BOOST_MMAP_IMPL() { namespace detail
{
    template <lifetime_policy, resizing_policy>
    struct named_memory_impl;
} }

template <lifetime_policy lifetime, resizing_policy resizability>
using named_memory = typename detail::named_memory_impl<lifetime, resizability>::type;

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // policies_hpp
