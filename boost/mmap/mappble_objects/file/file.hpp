////////////////////////////////////////////////////////////////////////////////
///
/// \file file.hpp
/// --------------
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
#ifndef file_hpp__D3705ED0_EC0D_4747_A789_1EE17252B6E2
#define file_hpp__D3705ED0_EC0D_4747_A789_1EE17252B6E2
#pragma once
//------------------------------------------------------------------------------
#include "../../detail/impl_selection.hpp"

#include BOOST_MMAP_IMPL_INCLUDE( ./, /file.hpp  )
#include BOOST_MMAP_IMPL_INCLUDE( ./, /flags.hpp )
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

typedef file_flags<BOOST_MMAP_IMPL> native_file_flags;

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // file_hpp
