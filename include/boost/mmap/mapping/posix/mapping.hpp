////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping.hpp
/// -----------------
///
/// Copyright (c) Domagoj Saric 2011 - 2019.
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
#ifndef mapping_hpp__99837E03_86B1_42F5_A57D_69A6E828DD08
#define mapping_hpp__99837E03_86B1_42F5_A57D_69A6E828DD08
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/handles/posix/handle.hpp"
#include "boost/mmap/flags/posix/mapping.hpp"

#include <boost/config/detail/suffix.hpp>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
BOOST_MMAP_POSIX_INLINE
namespace posix
{
//------------------------------------------------------------------------------

struct mapping
    :
    handle
{
    using native_handle_t = handle::native_handle_t        ;
    using reference       = mapping                 const &;

    static bool const owns_parent_handle = false;

    template <typename FileHandle>
    mapping( FileHandle && fd, flags::viewing const & view_mapping_flags_param, std::size_t const size ) noexcept
        : handle( std::forward<FileHandle>( fd ) ), view_mapping_flags( view_mapping_flags_param ), maximum_size( size ) {}

    bool is_read_only() const { return ( view_mapping_flags.protection & ( flags::access_privileges::write | flags::access_privileges::readwrite ) ) == 0; }

    flags::viewing const view_mapping_flags;
    std  ::size_t  const maximum_size;
}; // struct mapping


#ifdef PAGE_SIZE
std::uint16_t const page_size( PAGE_SIZE );
#else
BOOST_OVERRIDABLE_SYMBOL extern std::uint16_t const page_size
(
    ([]()
    {
        auto const size( ::getpagesize()/*::sysconf( _SC_PAGE_SIZE )*/ );
        BOOST_LIKELY( size     == 4096 );
        BOOST_ASSUME( size % 2 == 0    );
        return static_cast<std::uint16_t>( size );
    })()
);
#endif // PAGE_SIZE
BOOST_OVERRIDABLE_SYMBOL std::uint32_t const allocation_granularity( page_size );

//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // mapping_hpp
