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
#ifndef file_hpp__FB482005_18D9_4E3B_9193_A13DBFE88F45
#define file_hpp__FB482005_18D9_4E3B_9193_A13DBFE88F45
#pragma once
//------------------------------------------------------------------------------
#include "../../../handles/win32/handle.hpp"

#include <cstddef>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

struct win32_file_flags;

windows_handle create_file( char const * file_name, win32_file_flags const & );

bool        set_file_size( windows_handle::handle_t, std::size_t desired_size );
std::size_t get_file_size( windows_handle::handle_t                           );

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "file.inl"
#endif

#endif // file_hpp
