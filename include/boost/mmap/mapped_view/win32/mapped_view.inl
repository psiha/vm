////////////////////////////////////////////////////////////////////////////////
///
/// \file mapped_view.inl
/// ---------------------
///
/// Copyright (c) Domagoj Saric 2010 - 2021.
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

#include "boost/mmap/detail/impl_selection.hpp"
#include "boost/mmap/detail/win32.hpp"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winline-namespace-reopened-noninline"
#endif

//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
namespace win32
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
        std   ::uint64_t  const offset        , // ERROR_MAPPED_ALIGNMENT
        std   ::size_t    const desired_size
    ) noexcept
    {
        // Implementation note:
        // Mapped views hold internal references to the mapping handles so we do
        // not need to hold/store them ourselves:
        // http://msdn.microsoft.com/en-us/library/aa366537(VS.85).aspx
        //                                    (26.03.2010.) (Domagoj Saric)

        auto const large_integer{ reinterpret_cast<ULARGE_INTEGER const &>( offset ) };

        auto const view_start
        {
            static_cast<memory_range::value_type *>
            (
                ::MapViewOfFile
                (
                    source_mapping.get(),
                    flags.map_view_flags,
                    large_integer.HighPart,
                    large_integer.LowPart,
                    desired_size
                )
            )
        };

        return
        {
            view_start,
            view_start ? desired_size : 0
        };
    }

    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 )
    void BOOST_CC_REG unmap( memory_range const view )
    {
        BOOST_VERIFY( ::UnmapViewOfFile( view.data() ) || view.empty() );
    }
}; // struct mapper

//------------------------------------------------------------------------------
} // win32
//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------

#ifdef __clang__
#pragma clang diagnostic pop
#endif
