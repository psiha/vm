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
#ifndef handle_hpp__63113526_C3F1_46DC_850E_D8D8C62031DB
#define handle_hpp__63113526_C3F1_46DC_850E_D8D8C62031DB
#pragma once
//------------------------------------------------------------------------------
#ifdef BOOST_MSVC
    #include "../win32_file/handle.hpp"
#endif

#include "boost/assert.hpp"
#include "boost/noncopyable.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

struct posix_file_flags;

namespace guard
{
//------------------------------------------------------------------------------

class posix_handle
#ifdef BOOST_MSVC
    : noncopyable
#endif // BOOST_MSVC
{
public:
    typedef int handle_t;

    explicit posix_handle( handle_t );
    #ifndef BOOST_MSVC
        posix_handle( posix_handle const & );
    #endif // BOOST_MSVC

    #ifdef BOOST_MSVC
        explicit posix_handle( windows_handle::handle_t );
    #endif // _WIN32

    ~posix_handle();

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

#define BOOST_MMAP_IMPL_FILE "handle.inl"
#include "../../detail/include_impl_file.hpp"

#endif // handle_hpp
