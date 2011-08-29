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

#include "../../detail/windows.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <>
BOOST_IMPL_INLINE
mapped_view_reference<unsigned char, win32> mapped_view_reference<unsigned char, win32>::map
(
    mapping<win32>  const &       source_mapping,
    boost::uint64_t         const offset        ,
    std  ::size_t           const desired_size
)
{
    // Implementation note:
    // Mapped views hold internal references to the following handles so we do
    // not need to hold/store them ourselves:
    // http://msdn.microsoft.com/en-us/library/aa366537(VS.85).aspx
    //                                        (26.03.2010.) (Domagoj Saric)

    typedef mapped_view_reference<unsigned char, win32>::iterator iterator_t;

    ULARGE_INTEGER large_integer;
    large_integer.QuadPart = offset;

    iterator_t const view_start
    (
        static_cast<iterator_t>
        (
            ::MapViewOfFile
            (
                source_mapping.get(),
                source_mapping.view_mapping_flags,
                large_integer.HighPart,
                large_integer.LowPart,
                desired_size
            )
        )
    );

    return mapped_view_reference<unsigned char>
    (
        view_start,
        view_start
            ? view_start + desired_size
            : view_start
    );
}


template <> BOOST_IMPL_INLINE
void detail::mapped_view_base<unsigned char const, win32>::unmap( detail::mapped_view_base<unsigned char const, win32> const & mapped_range )
{
    BOOST_VERIFY( ::UnmapViewOfFile( mapped_range.begin() ) || mapped_range.empty() );
}

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
