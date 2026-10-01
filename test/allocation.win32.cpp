////////////////////////////////////////////////////////////////////////////////
///
/// \file allocation.win32.cpp
/// --------------------------
///
/// Tests of the Win32 anonymous memory primitives over a range made of more
/// than one VM allocation - what growing a block in place by appending an
/// allocation after it produces (expand_back's fixed path), and what a single
/// NtFreeVirtualMemory/NtAllocateVirtualMemory call cannot span.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifdef _WIN32

#include <psi/vm/allocation.hpp>
#include <psi/vm/detail/nt.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace
{
    std::size_t constexpr granule{ reserve_granularity };

    MEMORY_BASIC_INFORMATION query( void const * const address ) noexcept
    {
        MEMORY_BASIC_INFORMATION info{};
        nt::NtQueryVirtualMemory( nt::current_process, const_cast<void *>( address ), nt::MemoryBasicInformation, &info, sizeof( info ), nullptr );
        return info;
    }

    // Two adjacent allocations of one granule each, at an address known to be
    // free (taken by a reservation and given back first).
    std::byte * two_adjacent_allocations() noexcept
    {
        std::size_t size{ 2 * granule };
        auto * const p{ static_cast<std::byte *>( reserve( size ) ) };
        if ( !p )
            return nullptr;
        free( p, size );
        if ( !allocate_fixed( p, granule, allocation_type::commit ) )
            return nullptr;
        if ( !allocate_fixed( p + granule, granule, allocation_type::commit ) )
        {
            free( p, granule );
            return nullptr;
        }
        return p;
    }
} // anonymous namespace

TEST( allocation, decommit_spans_adjacent_allocations )
{
    auto * const p{ two_adjacent_allocations() };
    ASSERT_NE( p, nullptr );
    ASSERT_NE( query( p ).AllocationBase, query( p + granule ).AllocationBase );
    std::memset( p, 0x5A, 2 * granule );

    // across the boundary: the second half of the first, the first half of the second
    decommit( p + granule / 2, granule );
    EXPECT_EQ( query( p                   ).State, static_cast<DWORD>( MEM_COMMIT  ) );
    EXPECT_EQ( query( p + granule / 2     ).State, static_cast<DWORD>( MEM_RESERVE ) );
    EXPECT_EQ( query( p + granule         ).State, static_cast<DWORD>( MEM_RESERVE ) );
    EXPECT_EQ( query( p + 3 * granule / 2 ).State, static_cast<DWORD>( MEM_COMMIT  ) );
    EXPECT_EQ( p[ 0 ]              , std::byte{ 0x5A } );
    EXPECT_EQ( p[ 2 * granule - 1 ], std::byte{ 0x5A } );

    // and committed again (zeroed), also across it
    ASSERT_TRUE( commit( p + granule / 2, granule ) );
    EXPECT_EQ( p[ granule / 2 ], std::byte{} );
    EXPECT_EQ( p[ granule     ], std::byte{} );
    free( p, 2 * granule );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
#endif // _WIN32
