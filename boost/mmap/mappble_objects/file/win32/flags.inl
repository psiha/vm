////////////////////////////////////////////////////////////////////////////////
///
/// \file flags.inl
/// ---------------
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
#include "flags.hpp"

#include "../../detail/impl_inline.hpp"

#include "boost/assert.hpp"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#include "windows.h"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

unsigned int const file_flags<win32>::handle_access_rights::read    = GENERIC_READ   ;
unsigned int const file_flags<win32>::handle_access_rights::write   = GENERIC_WRITE  ;
unsigned int const file_flags<win32>::handle_access_rights::execute = GENERIC_EXECUTE;

unsigned int const file_flags<win32>::share_mode::none   = 0                ;
unsigned int const file_flags<win32>::share_mode::read   = FILE_SHARE_READ  ;
unsigned int const file_flags<win32>::share_mode::write  = FILE_SHARE_WRITE ;
unsigned int const file_flags<win32>::share_mode::remove = FILE_SHARE_DELETE;

unsigned int const file_flags<win32>::system_hints::random_access     = FILE_FLAG_RANDOM_ACCESS                         ;
unsigned int const file_flags<win32>::system_hints::sequential_access = FILE_FLAG_SEQUENTIAL_SCAN                       ;
unsigned int const file_flags<win32>::system_hints::non_cached        = FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;
unsigned int const file_flags<win32>::system_hints::delete_on_close   = FILE_FLAG_DELETE_ON_CLOSE                       ;
unsigned int const file_flags<win32>::system_hints::temporary         = FILE_ATTRIBUTE_TEMPORARY                        ;

unsigned int const file_flags<win32>::on_construction_rights::read    = FILE_ATTRIBUTE_READONLY;
unsigned int const file_flags<win32>::on_construction_rights::write   = FILE_ATTRIBUTE_NORMAL  ;
unsigned int const file_flags<win32>::on_construction_rights::execute = FILE_ATTRIBUTE_NORMAL  ;

BOOST_IMPL_INLINE
file_flags<win32> file_flags<win32>::create
(
    unsigned int  const handle_access_flags   ,
    unsigned int  const share_mode            ,
    open_policy_t const open_flags            ,
    unsigned int  const system_hints          ,
    unsigned int  const on_construction_rights
)
{
    file_flags<win32> const flags =
    {
        handle_access_flags, // desired_access
        share_mode, // share_mode
        open_flags, // creation_disposition
        system_hints
            |
        (
            ( on_construction_rights & FILE_ATTRIBUTE_NORMAL )
                ? ( on_construction_rights & ~FILE_ATTRIBUTE_READONLY )
                :   on_construction_rights
        ) // flags_and_attributes
    };

    return flags;
}


BOOST_IMPL_INLINE
file_flags<win32> file_flags<win32>::create_for_opening_existing_files
(
    unsigned int const handle_access_flags,
    unsigned int const share_mode,
    bool         const truncate,
    unsigned int const system_hints
)
{
    return create
    (
        handle_access_flags,
        share_mode,
        truncate
            ? open_policy::open_and_truncate_existing
            : open_policy::open_existing,
        system_hints,
        0
    );
}


//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
