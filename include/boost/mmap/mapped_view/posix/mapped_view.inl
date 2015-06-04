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

namespace detail
{
    template <>
    struct mapper<char, posix>
    {
        static BOOST_ATTRIBUTES( BOOST_COLD, BOOST_EXCEPTIONLESS )
        basic_memory_range_t BOOST_CC_REG
        map
        (
            mapping<posix> const & source_mapping,
            std::uint64_t          offset        ,
            std::size_t            desired_size
        ) noexcept
        {
            using iterator = mapped_view<char, posix>::iterator;

            iterator const view_start
            (
                static_cast<iterator>
                (
                    ::mmap
                    (
                        nullptr,
                        desired_size,
                        source_mapping.view_mapping_flags.protection,
                        source_mapping.view_mapping_flags.flags,
                        source_mapping,
                        offset
                    )
                )
            );

            return
                BOOST_LIKELY( view_start != MAP_FAILED )
                    ? basic_memory_range_t{ view_start, view_start + desired_size }
                    : basic_memory_range_t{ nullptr, nullptr };
        }

        static BOOST_ATTRIBUTES( BOOST_COLD, BOOST_EXCEPTIONLESS )
        void BOOST_CC_REG unmap( basic_memory_range_t const view )
        {
            BOOST_VERIFY
            (
                ( ::munmap( view.begin(), view.size() ) == 0 ) ||
                ( view.empty() && !view.begin() )
            );
        }
    }; // struct mapper<char, posix>
} // namespace detail

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
