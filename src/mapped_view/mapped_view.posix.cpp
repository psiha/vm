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
#include "mapper.hpp"

#include <psi/vm/align.hpp>
#include <psi/vm/detail/posix.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>

#include <boost/assert.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace posix
{
//------------------------------------------------------------------------------
namespace
{
#if !defined( MAP_UNINITIALIZED )
    auto constexpr MAP_UNINITIALIZED{ 0 };
#endif // MAP_UNINITIALIZED
#if !defined( MAP_ALIGNED_SUPER )
    auto constexpr MAP_ALIGNED_SUPER{ 0 }; // FreeBSD specfic hint for large pages https://man.freebsd.org/cgi/man.cgi?sektion=2&query=mmap
#endif // MAP_ALIGNED_SUPER
} // anonymous namespace

void * mmap( void * const target_address, std::size_t const size, int const protection, int const flags, int const file_handle, std::uint64_t const offset ) noexcept
{
    BOOST_ASSUME( is_aligned( target_address, reserve_granularity )                      );
    BOOST_ASSUME( is_aligned( size          , reserve_granularity ) || file_handle != -1 );

    auto const actual_address{ ::mmap( target_address, size, protection,
#   if defined( __linux__ ) && !defined( __ANDROID__ ) && 0 // investigate whether always wired
        MAP_HUGETLB |
#   endif
        MAP_UNINITIALIZED | MAP_ALIGNED_SUPER | flags,
        file_handle,
        static_cast< off_t >( offset )
    ) };
    auto const succeeded{ actual_address != MAP_FAILED };
    if ( succeeded ) [[ likely ]]
    {
        BOOST_ASSUME( !target_address || ( actual_address == target_address ) || ( ( flags & MAP_FIXED ) == 0 ) );
        BOOST_ASSUME( is_aligned( actual_address, reserve_granularity ) );
        return actual_address;
    }

    return nullptr;
}

//------------------------------------------------------------------------------
} // posix
//------------------------------------------------------------------------------

BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
mapped_span BOOST_CC_REG
mapper::map
(
    handle::reference const source_mapping,
    flags ::viewing   const flags         ,
    std   ::uint64_t  const offset        ,
    std   ::size_t    const desired_size
) noexcept
{
    /// \note mmap() explicitly rejects a zero length/desired_size, IOW
    /// unlike with MapViewOfFile() that approach cannot be used to
    /// automatically map the entire object - a valid size must be
    /// specified.
    /// http://man7.org/linux/man-pages/man2/mmap.2.html
    ///                               (30.09.2015.) (Domagoj Saric)

    auto const view_start
    {
        static_cast<mapped_span::value_type *>
        (
            posix::mmap
            (
                nullptr,
                desired_size,
                flags.protection,
                flags.flags,
                source_mapping,
                offset
            )
        )
    };

    return mapped_span{ view_start, view_start ? desired_size : 0 };
}

BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
void BOOST_CC_REG mapper::unmap( mapped_span const view ) noexcept
{
    [[ maybe_unused ]] auto munmap_result{ ::munmap( view.data(), view.size() ) };
#   ifndef __EMSCRIPTEN__
    BOOST_VERIFY
    (
        ( munmap_result == 0 ) ||
        ( view.empty() && !view.data() )
    );
#   endif
}

void mapper::shrink( mapped_span const view, std::size_t const target_size ) noexcept
{
    free
    (
        align_up  ( view.data() + target_size, commit_granularity ),
        align_down( view.size() - target_size, commit_granularity )
    );
}

void mapper::flush( mapped_span const view ) noexcept
{
    BOOST_VERIFY( ::msync( view.data(), view.size(), MS_ASYNC /*?: | MS_INVALIDATE*/ ) == 0 );
}

//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
