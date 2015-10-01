////////////////////////////////////////////////////////////////////////////////
///
/// \file flags.inl
/// ---------------
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
#include "flags.hpp"

#include "boost/mmap/detail/win32.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

static_assert( (unsigned)shared_memory<win32>::system_hints::default                    == SEC_COMMIT , "Boost.MMAP internal inconsistency" );
static_assert( (unsigned)shared_memory<win32>::system_hints::only_reserve_address_space == SEC_RESERVE, "Boost.MMAP internal inconsistency" );


BOOST_IMPL_INLINE
shared_memory<win32> BOOST_CC_REG shared_memory<win32>::create
(
    access_privileges<win32>                            const ap,
    named_object_construction_policy<win32>::value_type const nocp,
    system_hints                                        const system_hint
) noexcept
{
    auto flags( mapping<win32>::create( ap, nocp, share_mode::shared ) );
    flags.create_mapping_flags |= static_cast<flags_t>( system_hint );
    return static_cast<shared_memory<win32> &&>( flags );
}

//------------------------------------------------------------------------------
} // flags
//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
