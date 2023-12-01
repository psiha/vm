////////////////////////////////////////////////////////////////////////////////
///
/// \file flags.inl
/// ---------------
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
#include "flags.hpp"

#include "psi/vm/detail/win32.hpp"
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------
namespace win32
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

static_assert( (unsigned)shared_memory::system_hints::default                    == SEC_COMMIT , "Boost.MMAP internal inconsistency" );
static_assert( (unsigned)shared_memory::system_hints::only_reserve_address_space == SEC_RESERVE, "Boost.MMAP internal inconsistency" );


BOOST_IMPL_INLINE
shared_memory BOOST_CC_REG shared_memory::create
(
    access_privileges                const ap,
    named_object_construction_policy const nocp,
    system_hints                     const system_hint
) noexcept
{
    auto flags( mapping::create( ap, nocp, share_mode::shared ) );
    flags.create_mapping_flags |= static_cast<flags_t>( system_hint );
    return static_cast<shared_memory &&>( flags );
}

//------------------------------------------------------------------------------
} // flags
//------------------------------------------------------------------------------
} // win32
//------------------------------------------------------------------------------
} // vm
//------------------------------------------------------------------------------
} // psi
//------------------------------------------------------------------------------
