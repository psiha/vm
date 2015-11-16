////////////////////////////////////////////////////////////////////////////////
///
/// \file mapped_view.inl
/// ---------------------
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
#include "boost/mmap/mapped_view/mapped_view.hpp"

#include "boost/mmap/detail/posix.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
inline namespace posix
{
//------------------------------------------------------------------------------

struct mapper
{
    using error_t   = error;
    using flags_t   = flags::mapping;
    using mapping_t = mapping;

    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
    memory_range BOOST_CC_REG
    map
    (
        handle::reference const source_mapping,
        flags ::viewing   const flags         ,
        std   ::uint64_t  const offset        ,
        std   ::size_t    const desired_size
    ) noexcept
    {
        using iterator = mapped_view::iterator;

        /// \note mmap() explicitly rejects a zero length/desired_size, IOW
        /// unlike with MapViewOfFile() that approach cannot be used to
        /// automatically map the entire object - a valid size must be
        /// specified.
        /// http://man7.org/linux/man-pages/man2/mmap.2.html
        ///                               (30.09.2015.) (Domagoj Saric)

        iterator const view_start
        (
            static_cast<iterator>
            (
                ::mmap
                (
                    nullptr,
                    desired_size,
                    flags.protection,
                    flags.flags,
                    source_mapping,
                    offset
                )
            )
        );

        return
            BOOST_LIKELY( view_start != MAP_FAILED )
                ? memory_range{ view_start, view_start + desired_size }
                : memory_range{ nullptr, nullptr };
    }

    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
    void BOOST_CC_REG unmap( memory_range const view )
    {
        BOOST_VERIFY
        (
            ( ::munmap( view.begin(), view.size() ) == 0 ) ||
            ( view.empty() && !view.begin() )
        );
    }
}; // struct mapper

//------------------------------------------------------------------------------
} // posix
//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
