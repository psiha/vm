////////////////////////////////////////////////////////////////////////////////
///
/// \file file/handle.hpp
/// ---------------------
///
/// Copyright (c) Domagoj Saric 2010 - 2024.
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
#include "psi/vm/handles/handle.hpp"
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------

#ifdef BOOST_MSVC
    #pragma warning( push )
    #pragma warning( disable : 4510 ) // Default constructor was implicitly defined as deleted.
#endif // BOOST_MSVC


struct file_handle : PSI_VM_IMPL()::handle
{
    using handle::handle;

    using reference = typename handle::reference;

    operator reference () const noexcept { return reference{ this->get() }; }
}; // struct file_handle

#ifdef BOOST_MSVC
    #pragma warning( pop )
#endif // BOOST_MSVC

//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------
#endif // handle_hpp
