////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2010 - 2024.
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
#include <psi/vm/flags/mapping.win32.hpp>

#include <psi/vm/detail/win32.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace win32
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

static_assert(         ( viewing::access_rights::read    & 0xFF ) == SECTION_MAP_READ            , "Psi.VM internal inconsistency" );
static_assert(         ( viewing::access_rights::write   & 0xFF ) == SECTION_MAP_WRITE           , "Psi.VM internal inconsistency" );
static_assert(         ( viewing::access_rights::execute & 0xFF ) == SECTION_MAP_EXECUTE_EXPLICIT, "Psi.VM internal inconsistency" );

static_assert( unsigned( viewing::share_mode   ::shared         ) == 0             , "Psi.VM internal inconsistency" );
static_assert( unsigned( viewing::share_mode   ::hidden         ) == PAGE_WRITECOPY, "Psi.VM internal inconsistency" );

viewing viewing::create( access_privileges::object const object_access, share_mode const share_mode ) noexcept
{
    return { detail::object_access_to_page_access( object_access, share_mode ) };
}

bool viewing::is_cow() const noexcept { return page_protection & ( PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY ); }


namespace detail
{
    flags_t object_access_to_page_access( access_privileges::object const object_access, viewing::share_mode const share_mode ) noexcept
    {
        // Generate CreateFileMapping flags from access_privileges::object/MapViewOfFile flags
        using access_rights = access_privileges;
        auto const combined_handle_access_flags( object_access.privileges );
        flags_t page_protection
        (
            ( combined_handle_access_flags & access_rights::execute ) ? PAGE_EXECUTE : PAGE_NOACCESS
        );
        static_assert( PAGE_READONLY          == PAGE_NOACCESS * 2, "" );
        static_assert( PAGE_READWRITE         == PAGE_NOACCESS * 4, "" );
        static_assert( PAGE_WRITECOPY         == PAGE_NOACCESS * 8, "" );
        static_assert( PAGE_EXECUTE_READ      == PAGE_EXECUTE  * 2, "" );
        static_assert( PAGE_EXECUTE_READWRITE == PAGE_EXECUTE  * 4, "" );
        static_assert( PAGE_EXECUTE_WRITECOPY == PAGE_EXECUTE  * 8, "" );
        if ( share_mode == viewing::share_mode::hidden ) // WRITECOPY
            page_protection *= 8;
        else
        if ( combined_handle_access_flags & access_rights::write )
            page_protection *= 4;
        else
        {
            BOOST_ASSUME( combined_handle_access_flags & access_rights::read );
            page_protection *= 2;
        }

        return page_protection;
    }
} // namespace detail


mapping mapping::create
(
    access_privileges                const ap,
    named_object_construction_policy const construction_policy,
    share_mode                       const share_mode
) noexcept
{
    return
    {
        detail::object_access_to_page_access( ap.object_access, share_mode ),
        ap,
        construction_policy
    };
}

//------------------------------------------------------------------------------
} // flags
//------------------------------------------------------------------------------
} // win32
//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
