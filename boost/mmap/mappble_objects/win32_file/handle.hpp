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
#ifndef handle_hpp__1CEA6D65_D5C0_474E_833D_2CE927A1C74D
#define handle_hpp__1CEA6D65_D5C0_474E_833D_2CE927A1C74D
#pragma once
//------------------------------------------------------------------------------
#include "boost/assert.hpp"
#include "boost/noncopyable.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

struct win32_file_flags;

namespace guard
{
//------------------------------------------------------------------------------

class windows_handle : noncopyable
{
public:
    typedef void * handle_t;

    typedef win32_file_flags flags;

    explicit windows_handle( handle_t );
    ~windows_handle();

    handle_t const & handle() const { return handle_; }

private:
    handle_t const handle_;
};

#ifdef _WIN32
    typedef windows_handle native_handle;
#else
    typedef posix_handle   native_handle;
#endif // _WIN32
typedef native_handle::handle_t native_handle_t;

//------------------------------------------------------------------------------
} // namespace guard

guard::native_handle create_file( char const * file_name, guard::native_handle::flags const &                            );
guard::native_handle create_file( char const * file_name, guard::native_handle::flags const &, unsigned int desired_size );

bool        set_file_size( guard::native_handle_t, std::size_t desired_size );
std::size_t get_file_size( guard::native_handle_t                           );

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "handle.inl"
#endif

#endif // handle_hpp
