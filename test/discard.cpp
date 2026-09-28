////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
/// (See accompanying file LICENSE_1_0.txt or copy at
/// http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
#include <psi/vm/allocation.hpp>
#include <psi/vm/mapped_view/ops.hpp>
#include <psi/vm/span.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// Regression test: discard() must not treat a *successful* call as a failure.
// On Windows, DiscardVirtualMemory() returns a DWORD error code (ERROR_SUCCESS
// == 0 on success), not a BOOL - a plain truthiness check on its result is
// therefore inverted and fires precisely when the call succeeds.
TEST( discard, on_a_committed_private_region_does_not_report_failure )
{
    std::size_t size{ commit_granularity };
    auto * const memory{ static_cast<std::byte *>( allocate( size ) ) };
    ASSERT_NE( memory, nullptr );
    ASSERT_GE( size, commit_granularity );

    std::memset( memory, 0x5a, size );

    // Must complete without asserting/crashing (the bug fired a debug
    // assertion failure - and silently swallowed a real failure in
    // release - on every successful call).
    discard( mapped_span{ std::span{ memory, size } } );

    // The region remains committed and writable after the discard.
    memory[ 0 ]        = std::byte{ 0x11 };
    memory[ size - 1 ] = std::byte{ 0x22 };
    EXPECT_EQ( memory[ 0 ]       , std::byte{ 0x11 } );
    EXPECT_EQ( memory[ size - 1 ], std::byte{ 0x22 } );

    free( memory, size );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
