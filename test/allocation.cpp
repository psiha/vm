////////////////////////////////////////////////////////////////////////////////
///
/// \file allocation.cpp
/// --------------------
///
/// Tests of the POSIX anonymous memory primitives (reserve(), allocate(),
/// allocate_fixed(), commit()) and of growth through expand_back() and
/// expand_front().
///
/// The Win32 backend is built on NtAllocateVirtualMemory and is exercised
/// indirectly by every container test; these tests pin the mmap()-based
/// backend, which nothing else reaches directly.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef _WIN32

#include <psi/vm/allocation.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <csignal>
#include <cstring>

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace
{
    std::size_t constexpr granule{ reserve_granularity };

    bool all_zero( std::byte const * const p, std::size_t const size ) noexcept
    {
        return std::all_of( p, p + size, []( std::byte const b ) { return b == std::byte{}; } );
    }

    // Maps a range outside of the functions under test, so that a failure is
    // attributed to the function being tested rather than to its setup.
    std::byte * raw_map( std::size_t const size, int const protection ) noexcept
    {
        auto const p{ ::mmap( nullptr, size, protection, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 ) };
        return ( p == MAP_FAILED ) ? nullptr : static_cast<std::byte *>( p );
    }
} // anonymous namespace

TEST( allocation, reserve_returns_an_inaccessible_reservation )
{
    std::size_t size{ 3 * granule - 1 };
    errno = 0;
    auto * const p{ static_cast<std::byte *>( reserve( size ) ) };
    ASSERT_NE( p, nullptr ) << "errno " << errno << ": " << std::strerror( errno );
    EXPECT_EQ( size, 3 * granule );

    // PROT_NONE: touching the reservation must fault.
    // (SIGSEGV on Linux, SIGBUS on Darwin.)
    auto const killed_by_a_memory_fault{ []( int const status ) noexcept
    {
        return WIFSIGNALED( status ) && ( ( WTERMSIG( status ) == SIGSEGV ) || ( WTERMSIG( status ) == SIGBUS ) );
    } };
    EXPECT_EXIT( { *static_cast<std::byte volatile *>( p ) = std::byte{ 1 }; ::_exit( 0 ); }, killed_by_a_memory_fault, "" );

    // Once committed it is usable, and reads back as zero.
    ASSERT_TRUE( commit( p, size ) );
    EXPECT_TRUE( all_zero( p, size ) );
    std::memset( p, 0x5A, size );
    EXPECT_EQ( p[ size - 1 ], std::byte{ 0x5A } );
    free( p, size );
}

TEST( allocation, allocate_returns_writable_zeroed_memory )
{
    std::size_t size{ 2 * granule };
    errno = 0;
    auto * const p{ static_cast<std::byte *>( allocate( size ) ) };
    ASSERT_NE( p, nullptr ) << "errno " << errno << ": " << std::strerror( errno );
    EXPECT_EQ( size, 2 * granule );
    EXPECT_TRUE( all_zero( p, size ) );
    std::memset( p, 0xA5, size );
    EXPECT_EQ( p[ 0        ], std::byte{ 0xA5 } );
    EXPECT_EQ( p[ size - 1 ], std::byte{ 0xA5 } );
    free( p, size );
}

TEST( allocation, allocate_fixed_maps_a_free_range_at_the_requested_address )
{
    // Obtain an address range known to be free: map two granules and unmap
    // the second one.
    auto * const head{ raw_map( 2 * granule, PROT_READ | PROT_WRITE ) };
    ASSERT_NE( head, nullptr );
    auto * const tail{ head + granule };
    ASSERT_EQ( ::munmap( tail, granule ), 0 );

    errno = 0;
    ASSERT_TRUE( allocate_fixed( tail, granule, allocation_type::commit ) ) << "errno " << errno << ": " << std::strerror( errno );
    EXPECT_TRUE( all_zero( tail, granule ) );
    std::memset( tail, 0x3C, granule );
    EXPECT_EQ( tail[ granule - 1 ], std::byte{ 0x3C } );
    free( head, 2 * granule );
}

TEST( allocation, allocate_fixed_reserves_a_free_range_at_the_requested_address )
{
    auto * const head{ raw_map( 2 * granule, PROT_READ | PROT_WRITE ) };
    ASSERT_NE( head, nullptr );
    auto * const tail{ head + granule };
    ASSERT_EQ( ::munmap( tail, granule ), 0 );

    errno = 0;
    ASSERT_TRUE( allocate_fixed( tail, granule, allocation_type::reserve ) ) << "errno " << errno << ": " << std::strerror( errno );
    ASSERT_TRUE( commit( tail, granule ) );
    EXPECT_TRUE( all_zero( tail, granule ) );
    free( head, 2 * granule );
}

TEST( allocation, allocate_fixed_does_not_replace_an_existing_mapping )
{
    // Same contract as the Win32 backend (MEM_RESERVE over an existing
    // reservation fails): an occupied range is refused, not clobbered.
    auto * const p{ raw_map( granule, PROT_READ | PROT_WRITE ) };
    ASSERT_NE( p, nullptr );
    std::memset( p, 0x77, granule );

    EXPECT_FALSE( allocate_fixed( p, granule, allocation_type::commit ) );
    EXPECT_EQ( p[ 0           ], std::byte{ 0x77 } );
    EXPECT_EQ( p[ granule - 1 ], std::byte{ 0x77 } );
    free( p, granule );
}

TEST( allocation, expand_back_grows_in_place_into_free_address_space )
{
    // Map the final extent, then give its tail back, so that free address
    // space is known to follow the block to be grown.
    auto * const p{ raw_map( 4 * granule, PROT_READ | PROT_WRITE ) };
    ASSERT_NE( p, nullptr );
    ASSERT_EQ( ::munmap( p + granule, 3 * granule ), 0 );
    std::memset( p, 0x42, granule );

    auto const result{ expand_back( { p, granule }, 4 * granule, granule, allocation_type::commit, reallocation_type::fixed ) };
    ASSERT_TRUE( result );
    EXPECT_EQ( result.method, expand_result::back_extended );
    EXPECT_EQ( result.new_span.data(), p );
    EXPECT_EQ( result.new_span.size(), 4 * granule );

    // The original contents are kept and the new part is usable and zeroed.
    EXPECT_EQ( p[ 0           ], std::byte{ 0x42 } );
    EXPECT_EQ( p[ granule - 1 ], std::byte{ 0x42 } );
    EXPECT_TRUE( all_zero( p + granule, 3 * granule ) );
    std::memset( p + granule, 0x24, 3 * granule );
    EXPECT_EQ( p[ 4 * granule - 1 ], std::byte{ 0x24 } );
    free( p, 4 * granule );
}

TEST( allocation, expand_front_grows_in_place_into_free_address_space )
{
    // Map the final extent, then give its head back, so that free address
    // space is known to precede the block to be grown.
    auto * const head{ raw_map( 4 * granule, PROT_READ | PROT_WRITE ) };
    ASSERT_NE( head, nullptr );
    auto * const p{ head + 3 * granule };
    ASSERT_EQ( ::munmap( head, 3 * granule ), 0 );
    std::memset( p, 0x42, granule );

    auto const result{ expand_front( { p, granule }, 4 * granule, granule, allocation_type::commit, reallocation_type::fixed ) };
    ASSERT_TRUE( result );
    EXPECT_EQ( result.method, expand_result::front_extended );
    EXPECT_EQ( result.new_span.data(), head );
    EXPECT_EQ( result.new_span.size(), 4 * granule );

    // The original contents stay where they were, at the tail of the grown
    // block, and the prepended part is usable and zeroed.
    EXPECT_EQ( p[ 0           ], std::byte{ 0x42 } );
    EXPECT_EQ( p[ granule - 1 ], std::byte{ 0x42 } );
    EXPECT_TRUE( all_zero( head, 3 * granule ) );
    std::memset( head, 0x24, 3 * granule );
    EXPECT_EQ( head[ 0 ], std::byte{ 0x24 } );
    free( head, 4 * granule );
}

TEST( allocation, expand_front_moves_when_the_space_before_is_taken )
{
    // An occupied granule right before the block leaves no room to prepend.
    auto * const guard{ raw_map( 2 * granule, PROT_READ | PROT_WRITE ) };
    ASSERT_NE( guard, nullptr );
    auto * const p{ guard + granule };
    std::memset( p, 0x42, granule );

    auto const result{ expand_front( { p, granule }, 4 * granule, granule, allocation_type::commit, reallocation_type::moveable ) };
    ASSERT_TRUE( result );
    EXPECT_EQ( result.method, expand_result::moved );
    ASSERT_EQ( result.new_span.size(), 4 * granule );

    // Same layout as an in-place front expansion: the old block becomes the
    // tail of the new one.
    auto * const moved{ result.new_span.data() };
    EXPECT_TRUE( all_zero( moved, 3 * granule ) );
    EXPECT_EQ( moved[ 3 * granule     ], std::byte{ 0x42 } );
    EXPECT_EQ( moved[ 4 * granule - 1 ], std::byte{ 0x42 } );
    free( moved, 4 * granule );
    free( guard, granule ); // the old block was released by the move
}

TEST( allocation, commit_of_an_unmapped_range_fails )
{
    auto * const p{ raw_map( granule, PROT_NONE ) };
    ASSERT_NE( p, nullptr );
    ASSERT_EQ( ::munmap( p, granule ), 0 );
    EXPECT_FALSE( commit( p, granule ) );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------

#endif // !_WIN32
