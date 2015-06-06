////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping.hpp
/// -----------------
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
#ifndef mapping_hpp__99837E03_86B1_42F5_A57D_69A6E828DD08
#define mapping_hpp__99837E03_86B1_42F5_A57D_69A6E828DD08
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/handles/posix/handle.hpp"
#include "boost/mmap/flags/posix/mapping.hpp"

#include "boost/config/suffix.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl> struct mapping;

template <>
struct mapping<posix>
    :
    handle<posix>::reference
{
    using native_handle_t = handle<posix>::native_handle_t        ;
    using reference       = mapping                        const &;

    static bool const owns_parent_handle = false;

    mapping( native_handle_t const native_handle, flags::mapping<posix> const & view_mapping_flags_param ) noexcept
        : handle<posix>::reference{ native_handle }, view_mapping_flags( view_mapping_flags_param ) {}

    bool is_read_only() const { return ( view_mapping_flags.protection & flags::mapping<posix>::access_rights::write ) == 0; }

    flags::mapping<posix> const view_mapping_flags;
}; // struct mapping<posix>


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
BOOST_OVERRIDABLE_SYMBOL extern std::uint32_t const allocation_granularity( page_size );

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // mapping_hpp
