////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping_flags.inl
/// -----------------------
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
#include "mapping_flags.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

BOOST_IMPL_INLINE
file_mapping_flags<posix> file_mapping_flags<posix>::create
(
    flags_t                const combined_handle_access_flags,
    share_mode::value_type const share_mode
)
{
    file_mapping_flags<posix> flags;

    flags.protection = combined_handle_access_flags;
    flags.flags      = share_mode;

    return flags;
}

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
