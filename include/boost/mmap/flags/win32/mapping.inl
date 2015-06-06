////////////////////////////////////////////////////////////////////////////////
///
/// \file flags/win32/mapping.inl
/// -----------------------------
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
#ifndef mapping_inl__CD518463_D4CB_4E18_8E35_E0FBBA8CA1D1
#define mapping_inl__CD518463_D4CB_4E18_8E35_E0FBBA8CA1D1
#pragma once
//------------------------------------------------------------------------------
#include "mapping.hpp"

#include "boost/mmap/detail/impl_inline.hpp"
#include "boost/mmap/detail/win32.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

static_assert(           mapping<win32>::access_rights::read    == FILE_MAP_READ   , "Boost.MMAP internal inconsistency" );
static_assert(           mapping<win32>::access_rights::write   == FILE_MAP_WRITE  , "Boost.MMAP internal inconsistency" );
static_assert(           mapping<win32>::access_rights::execute == FILE_MAP_EXECUTE, "Boost.MMAP internal inconsistency" );

static_assert( (unsigned)mapping<win32>::share_mode   ::shared  == 0               , "Boost.MMAP internal inconsistency" );
static_assert( (unsigned)mapping<win32>::share_mode   ::hidden  == FILE_MAP_COPY   , "Boost.MMAP internal inconsistency" );


BOOST_IMPL_INLINE
mapping<win32> mapping<win32>::create
(
    flags_t    const combined_handle_access_flags,
    share_mode const share_mode
) noexcept
{
    // Generate CreateFileMapping flags from MapViewOfFile flags
    flags_t create_mapping_flags
    (
        ( combined_handle_access_flags & access_rights::execute ) ? PAGE_EXECUTE : PAGE_NOACCESS
    );
    static_assert( PAGE_READONLY          == PAGE_NOACCESS * 2, "" );
    static_assert( PAGE_READWRITE         == PAGE_NOACCESS * 4, "" );
    static_assert( PAGE_WRITECOPY         == PAGE_NOACCESS * 8, "" );
    static_assert( PAGE_EXECUTE_READ      == PAGE_EXECUTE  * 2, "" );
    static_assert( PAGE_EXECUTE_READWRITE == PAGE_EXECUTE  * 4, "" );
    static_assert( PAGE_EXECUTE_WRITECOPY == PAGE_EXECUTE  * 8, "" );
    if ( share_mode == share_mode::hidden ) // WRITECOPY
        create_mapping_flags *= 8;
    else
    if ( combined_handle_access_flags & access_rights::write )
        create_mapping_flags *= 4;
    else
    {
        BOOST_ASSERT( combined_handle_access_flags & access_rights::read );
        create_mapping_flags *= 2;
    }

    flags_t const map_view_flags( combined_handle_access_flags );

    return flags::mapping<win32> { create_mapping_flags, map_view_flags, nullptr };
}

//------------------------------------------------------------------------------
} // flags
//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
#endif // mapping.inl
