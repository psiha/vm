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
#include "boost/mmap/flags/win32/mapping.hpp"

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

static_assert(         ( viewing<win32>::access_rights::read    & 0xFF ) == FILE_MAP_READ   , "Boost.MMAP internal inconsistency" );
static_assert(         ( viewing<win32>::access_rights::write   & 0xFF ) == FILE_MAP_WRITE  , "Boost.MMAP internal inconsistency" );
static_assert(         ( viewing<win32>::access_rights::execute & 0xFF ) == FILE_MAP_EXECUTE, "Boost.MMAP internal inconsistency" );

static_assert( (unsigned)viewing<win32>::share_mode   ::shared           == 0               , "Boost.MMAP internal inconsistency" );
static_assert( (unsigned)viewing<win32>::share_mode   ::hidden           == FILE_MAP_COPY   , "Boost.MMAP internal inconsistency" );

BOOST_IMPL_INLINE
viewing<win32> viewing<win32>::create
(
    access_privileges<win32>::object const object_access,
    share_mode                       const share_mode
) noexcept
{
    /// \note According to the explicit documentation for the FILE_MAP_WRITE
    /// value, it can actually be combined with FILE_MAP_READ (it is also
    /// implicit from the definition of the FILE_MAP_ALL_ACCESS value).
    /// https://msdn.microsoft.com/en-us/library/aa366542(v=vs.85).aspx
    ///                                       (11.08.2015.) (Domagoj Saric)
    flags_t combined_handle_access_flags( object_access.privileges & 0xFF );
    if ( BOOST_UNLIKELY( share_mode == share_mode::hidden ) )
    {
        combined_handle_access_flags &= ~access_rights::write;
        combined_handle_access_flags |= static_cast<flags_t>( share_mode::hidden );
    }
    return { combined_handle_access_flags };
}


BOOST_IMPL_INLINE
bool viewing<win32>::is_cow() const
{
    /// \note Mind the Win32+NativeNT flags mess: FILE_MAP_ALL_ACCESS maps to
    /// (NativeNT) SECTION_ALL_ACCESS which includes SECTION_QUERY which in
    /// turn has the same value as FILE_MAP_COPY (which according to
    /// MapViewOfFile() documentation, is supposed to be a 'distinct' flag
    /// WRT the FILE_MAP_ALL_ACCESS flag).
    ///                                       (05.09.2015.) (Domagoj Saric)
    static_assert( FILE_MAP_ALL_ACCESS & FILE_MAP_COPY, "" );
    return ( map_view_flags & FILE_MAP_ALL_ACCESS ) == FILE_MAP_COPY;
}


namespace detail
{
    BOOST_IMPL_INLINE
    flags_t BOOST_CC_REG object_access_to_page_access( access_privileges<win32>::object const object_access, viewing<win32>::share_mode const share_mode )
    {
        // Generate CreateFileMapping flags from access_privileges<win32>::object/MapViewOfFile flags
        using access_rights = access_privileges<win32>;
        auto const combined_handle_access_flags( object_access.privileges );
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
        if ( share_mode == viewing<win32>::share_mode::hidden ) // WRITECOPY
            create_mapping_flags *= 8;
        else
        if ( combined_handle_access_flags & access_rights::write )
            create_mapping_flags *= 4;
        else
        {
            BOOST_ASSUME( combined_handle_access_flags & access_rights::read );
            create_mapping_flags *= 2;
        }

        return create_mapping_flags;
    }
} // namespace detail


BOOST_IMPL_INLINE
mapping<win32> mapping<win32>::create
(
    access_privileges<win32>                            const ap,
    named_object_construction_policy<win32>::value_type const construction_policy,
    share_mode                                          const share_mode
) noexcept
{
    return
    {
        detail::object_access_to_page_access( ap.object_access, share_mode ),
        ap.object_access    ,
        ap.child_access     ,
        ap.system_access    ,
        construction_policy ,
        viewing<win32>::create( ap.object_access, share_mode )
    };
}

//------------------------------------------------------------------------------
} // flags
//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
#endif // mapping.inl
