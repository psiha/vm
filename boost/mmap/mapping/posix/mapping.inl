////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping.inl
/// -----------------
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
#include "mapping.hpp"

#include "../../detail/posix.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <> BOOST_IMPL_INLINE
mapped_view_reference<unsigned char, posix> mapped_view_reference<unsigned char, posix>::map
(
    mapping<posix>  const &       source_mapping,
    boost::uint64_t         const offset        ,
    std  ::size_t           const desired_size
)
{
    iterator const view_start
    (
        static_cast<iterator>
        (
            ::mmap
            (
                0,
                desired_size,
                source_mapping.view_mapping_flags.protection,
                source_mapping.view_mapping_flags.flags,
                source_mapping,
                0
            )
        )
    );

    return mapped_view_reference<unsigned char>
    (
        view_start,
        ( view_start != MAP_FAILED )
            ? view_start + desired_size
            : view_start
    );
}


template <> BOOST_IMPL_INLINE
void detail::mapped_view_base<unsigned char const, posix>::unmap( detail::mapped_view_base<unsigned char const, posix> const & mapped_range )
{
    BOOST_VERIFY( ( ::munmap( const_cast<unsigned char *>( mapped_range.begin() ), mapped_range.size() ) == 0 ) || mapped_range.empty() );
}

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
