////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping.hpp
/// -----------------
///
/// Copyright (c) Domagoj Saric 2011 - 2021.
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

    bool is_read_only() const { return ( static_cast< std::uint32_t >( view_mapping_flags.protection ) & ( flags::access_privileges::write | flags::access_privileges::readwrite ) ) == 0; }

    flags::viewing const view_mapping_flags;
    std  ::size_t  const maximum_size;
}; // struct mapping


#ifdef PAGE_SIZE
#   ifdef __APPLE__
inline std::uint32_t const page_size{ PAGE_SIZE }; // Not a constant expression on Apple platform
#   else
constexpr std::uint32_t const page_size{ PAGE_SIZE }; // Under Emscripten PAGE_SIZE is 64k/does not fit into a std::uint16_t
#endif
#else
inline std::uint16_t const page_size
(
    ([]() noexcept
    {
        auto const size( ::getpagesize()/*::sysconf( _SC_PAGE_SIZE )*/ );
        BOOST_LIKELY( size     == 4096 );
        BOOST_ASSUME( size % 2 == 0    );
        return static_cast<std::uint16_t>( size );
    })()
);
#endif // PAGE_SIZE
inline std::uint32_t const allocation_granularity( page_size );

//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // mapping_hpp
