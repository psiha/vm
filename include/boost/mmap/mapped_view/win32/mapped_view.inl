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

#include "boost/mmap/detail/win32.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <>
struct detail::mapper<char, win32>
{
    static BOOST_ATTRIBUTES( BOOST_COLD, BOOST_EXCEPTIONLESS )
    basic_memory_range_t BOOST_CC_REG
    map
    (
        mapping<win32> const & source_mapping,
        std::uint64_t          offset        ,
        std::size_t            desired_size
    ) noexcept
    {
        // Implementation note:
        // Mapped views hold internal references to the following handles so we
        // do not need to hold/store them ourselves:
        // http://msdn.microsoft.com/en-us/library/aa366537(VS.85).aspx
        //                                        (26.03.2010.) (Domagoj Saric)

        using iterator = mapped_view<char, win32>::iterator;

        ULARGE_INTEGER large_integer;
        large_integer.QuadPart = offset;

        iterator const view_start
        (
            static_cast<iterator>
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

        return
        {
            view_start,
            view_start + ( view_start ? desired_size : 0 )
        };
    }

    static BOOST_ATTRIBUTES( BOOST_COLD, BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 )
    void BOOST_CC_REG unmap( basic_memory_range_t const view )
    {
        BOOST_VERIFY( ::UnmapViewOfFile( view.begin() ) || view.empty() );
    }
}; // struct detail::mapper<char, win32>

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
