////////////////////////////////////////////////////////////////////////////////
///
/// \file mapped_view.inl
/// ---------------------
///
/// Copyright (c) Domagoj Saric 2010 - 2019.
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
#include "utility.hpp"

#include "file.hpp"

#include "boost/mmap/flags/flags.hpp"
#include "boost/mmap/detail/impl_inline.hpp"
#include "boost/mmap/detail/impl_selection.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

namespace detail0
{
    using opening             = flags::opening;
    using default_file_handle = file_handle   ;

    BOOST_IMPL_INLINE
    opening create_rw_file_flags()
    {
        using namespace flags;
        using ap = access_privileges;
        return opening::create
        (
            {
                ap::object{ ap::readwrite },
                ap::child_process::does_not_inherit,
                ap::system::process_default// ap::system::user( ap::readwrite ) | ap::system::group( ap::read )
            },
            named_object_construction_policy::open_or_create,
            system_hints                    ::sequential_access
        );
    }

    BOOST_IMPL_INLINE
    opening create_r_file_flags()
    {
        using namespace flags;
        using ap = access_privileges;
        return opening::create_for_opening_existing_objects
               (
                   ap::object{ ap::read },
                   ap::child_process::does_not_inherit,
                   system_hints::sequential_access,
                   false
               );
    }

    BOOST_IMPL_INLINE
    fallible_result<mapped_view> BOOST_CC_REG
    map_file( default_file_handle && file_handle, std::size_t /*const*/ desired_size ) noexcept
    {
        if ( BOOST_UNLIKELY( file_handle.get() == default_file_handle::traits::invalid_value ) )
            return error{};
        /// \note CreateFileMapping() automatically expands the file as
        /// necessary (but only if the file is opened with write access and the
        /// share_mode::hidden flag is not specified) so there is no need to
        /// call set_size() on the file_handle in case the file is to be
        /// enlarged or mapped whole - but this does cover the case when the
        /// file is to be shrinked.
        /// https://msdn.microsoft.com/en-us/library/aa366542(v=vs.85).aspx
        ///                                   (30.05.2015.) (Domagoj Saric)
        /// \note Even though Windows will map the entire file if 0 is passed
        /// here, we still have to get the file's size in order to know
        /// the size of the mapping/mapped view.
        ///                                   (23.03.2018.) (Domagoj Saric)
        // memadv http://stackoverflow.com/questions/13126167/is-it-safe-to-ftruncate-a-shared-memory-object-after-it-has-ben-mmaped
        if ( desired_size ) set_size( file_handle, desired_size );
        else desired_size = get_size( file_handle               );

        using ap    = flags::access_privileges;
        using flags = flags::mapping;
        return mapped_view::map
        (
            create_mapping
            (
                std::move( file_handle ),
                ap::object{ ap::readwrite },
                ap::child_process::does_not_inherit,
                flags::share_mode::shared,
                desired_size
            ),
            0, // no offset
            desired_size
        );
    }


    BOOST_IMPL_INLINE
    fallible_result<read_only_mapped_view>
    map_read_only_file( default_file_handle && file_handle ) noexcept
    {
        if ( BOOST_UNLIKELY( file_handle.get() == default_file_handle::traits::invalid_value ) )
            return error{};

        /// \note Even though Windows will map the entire file if 0 is
        /// passed here, we still have to get the file's size in order to
        /// know the size of the mapping/mapped view.
        ///                               (23.03.2018.) (Domagoj Saric)
        auto const size( get_size( file_handle ) );

        using ap    = flags::access_privileges;
        using flags = flags::mapping;
        return read_only_mapped_view::map
        (
            create_mapping
            (
                std::move( file_handle ),
                ap::object{ ap::read },
                ap::child_process::does_not_inherit,
                flags::share_mode::shared,
                0
            ),
            0, // no offset
            size
        );
    }
} // namespace detail0

BOOST_IMPL_INLINE BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
fallible_result<mapped_view> map_file( char const * const file_name, std::size_t const desired_size ) noexcept
{
    return detail0::map_file( create_file( file_name, detail0::create_rw_file_flags() ), desired_size );
}

BOOST_IMPL_INLINE BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
fallible_result<read_only_mapped_view> map_read_only_file( char const * const file_name ) noexcept
{
    return detail0::map_read_only_file( create_file( file_name, detail0::create_r_file_flags() ) );
}

#ifdef _WIN32
    BOOST_IMPL_INLINE BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
    fallible_result<mapped_view> map_file( wchar_t const * const file_name, std::size_t const desired_size )
    {
        return detail0::map_file( create_file( file_name, detail0::create_rw_file_flags() ), desired_size );
    }

    BOOST_IMPL_INLINE BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
    fallible_result<read_only_mapped_view> map_read_only_file( wchar_t const * const file_name )
    {
        return detail0::map_read_only_file( create_file( file_name, detail0::create_r_file_flags() ) );
    }
#endif // _WIN32

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------

#ifndef BOOST_MMAP_HEADER_ONLY
    #include "file.inl"
#endif // BOOST_MMAP_HEADER_ONLY
