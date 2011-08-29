////////////////////////////////////////////////////////////////////////////////
///
/// \file mapped_view.inl
/// ---------------------
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
#include "mapped_view.hpp"

#include "detail/impl_inline.hpp"
#include "implementations.hpp"
#include "mappble_objects/file/file.hpp"

#include "boost/assert.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

BOOST_IMPL_INLINE
basic_mapped_view_ref map_file( char const * const file_name, std::size_t desired_size )
{
    typedef file_open_flags<BOOST_MMAP_IMPL()> open_flags;
    file_handle<BOOST_MMAP_IMPL()> const file_handle
    (
        create_file
        (
            file_name,
            open_flags::create
            (
                open_flags::handle_access_rights  ::read | open_flags::handle_access_rights::write,
                open_flags::open_policy           ::open_or_create,
                open_flags::system_hints          ::sequential_access,
                open_flags::on_construction_rights::read | open_flags::on_construction_rights::write
            )
        )
    );

    if ( desired_size )
        set_size( file_handle.get(), desired_size );
    else
        desired_size = get_size( file_handle.get() );

    typedef file_mapping_flags<BOOST_MMAP_IMPL()> mapping_flags;
    return basic_mapped_view_ref::map
    (
        create_mapping
        (
            file_handle,
            mapping_flags::create
            (
                mapping_flags::handle_access_rights::read | mapping_flags::handle_access_rights::write,
                mapping_flags::share_mode          ::shared
            )
        ),
        0,
        desired_size
    );
}


BOOST_IMPL_INLINE
basic_mapped_read_only_view_ref map_read_only_file( char const * const file_name )
{
    typedef file_open_flags<BOOST_MMAP_IMPL()> open_flags;
    file_handle<BOOST_MMAP_IMPL()> const file_handle
    (
        create_file
        (
            file_name,
            open_flags::create_for_opening_existing_files
            (
                open_flags::handle_access_rights::read,
                false,
                open_flags::system_hints        ::sequential_access
            )
        )
    );

    typedef file_mapping_flags<BOOST_MMAP_IMPL()> mapping_flags;
    return basic_mapped_read_only_view_ref::map
    (
        create_mapping
        (
            file_handle,
            mapping_flags::create
            (
                mapping_flags::handle_access_rights::read,
                mapping_flags::share_mode          ::shared
            )
        ),
        0,
        get_size( file_handle.get() )
    );
}


//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
