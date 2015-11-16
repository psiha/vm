////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping.hpp
/// -----------------
///
/// Copyright (c) Domagoj Saric 2010 - 2015.
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
#ifndef mapping_hpp__D42BC724_FD9A_4C7B_B521_CF3C29C948B3
#define mapping_hpp__D42BC724_FD9A_4C7B_B521_CF3C29C948B3
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/detail/impl_selection.hpp"

#include BOOST_MMAP_IMPL_INCLUDE( BOOST_PP_EMPTY, BOOST_PP_IDENTITY( /mapping.hpp ) )

#include <cstdint>
#include <cstdio>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace filesystem { class path; }
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Handle>
struct is_mappable : std::false_type {};

template <> struct is_mappable<filesystem::path const  &> : std::true_type {}; // c_str()
template <> struct is_mappable<filesystem::path         > : std::true_type {};
template <> struct is_mappable<::FILE                  &> : std::true_type {};
template <> struct is_mappable<::FILE                  *> : std::true_type {};
template <> struct is_mappable<handle::native_handle_t  > : std::true_type {};
#ifdef _WIN32
template <> struct is_mappable<posix::handle::native_handle_t> : std::true_type {};
#endif // _WIN32
#if 0 // todo std iostream
// http://www.linuxquestions.org/questions/programming-9/%5Bcygwin%5D%5Bc-%5D-how-to-get-file-descriptor-using-istream-object-204737
// https://gcc.gnu.org/onlinedocs/gcc-4.6.2/libstdc++/api/a00069.html#a59f78806603c619eafcd4537c920f859
// http://www.ginac.de/~kreckel/fileno
// http://stackoverflow.com/questions/109449/getting-a-file-from-a-stdfstream
// http://cpptips.com/fstream
template <> struct is_mappable<std::iostream> : std::true_type{};
#endif // disabled/todo


#if 0 // todo

mapping create_mapping( handle::reference mappable_object, flags::mapping, std::uint64_t maximum_size, char const * name ) noexcept;

mapping create_mapping( FILE & c_file_stream, flags::mapping const flags, std::uint64_t const maximum_size, char const * const name ) noexcept
{
    return create_mapping( /*std::*/fileno( &c_file_stream ), flags, maximum_size, name );
}

mapping create_mapping( FILE * const p_c_file_stream, flags::mapping const flags, std::uint64_t const maximum_size, char const * const name ) noexcept
{
    return create_mapping( *p_c_file_stream, flags, maximum_size, name );
}

mapping open_mapping( mapping, char const * name ) noexcept;
#endif // todo

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // mapping_hpp
