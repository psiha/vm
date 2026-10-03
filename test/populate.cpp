////////////////////////////////////////////////////////////////////////////////
///
/// \file populate.cpp
/// ------------------
///
/// Tests for populate(): the pages it is asked to fault in are resident when
/// it returns true, only whole pages inside the range are touched, and where
/// the system cannot populate it says so and changes nothing.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include <psi/vm/allocation.hpp>
#include <psi/vm/containers/heap_vector.hpp>
#include <psi/vm/containers/vm_vector.hpp>
#include <psi/vm/populate.hpp>

#include "resident_pages.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>

#ifndef _WIN32
#   include <sys/mman.h>
#endif
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace
{
    std::size_t constexpr pages{ 4096 };
    std::size_t constexpr bytes{ pages * page_size };

    std::span<std::byte const> as_bytes( void const * const address, std::size_t const size ) noexcept
    {
        return { static_cast<std::byte const *>( address ), size };
    }

    // a fresh anonymous block of 'pages' pages, of which nothing is resident,
    // and which huge pages never back: with them (the system setting "always")
    // the kernel populates the whole 2 MiB page around a populated one, which
    // would hide what these tests ask about, single pages
    struct block
    {
        block() noexcept
        {
            std::size_t size{ bytes };
            address = allocate( size );
#       if defined( MADV_NOHUGEPAGE )
            if ( address )
                (void)::madvise( address, bytes, MADV_NOHUGEPAGE );
#       endif
        }
        ~block() { free( address, bytes ); }
        void * address;
    };
} // anonymous namespace

TEST( populate, makes_the_whole_range_resident )
{
    block b;
    ASSERT_NE( b.address, nullptr );
    auto const before{ resident_pages( as_bytes( b.address, bytes ) ) };
    if ( !before )
        GTEST_SKIP() << "residency cannot be asked here";
    EXPECT_EQ( *before, 0U ) << "a fresh block is not resident";

    for ( auto const access : { populate_access::write, populate_access::read } )
    {
        auto const done{ populate( b.address, bytes, access ) };
        auto const after{ resident_pages( as_bytes( b.address, bytes ) ) };
        ASSERT_TRUE( after );
        if ( can_populate() )
        {
            EXPECT_TRUE( done );
            EXPECT_EQ( *after, pages ) << ( access == populate_access::write ? "write" : "read" );
        }
        else
        {
            EXPECT_FALSE( done );
            EXPECT_EQ( *after, 0U ) << "nothing was done, nothing may be resident";
        }
        break; // the read access only maps the zero page: not asked about residency twice
    }
}

TEST( populate, touches_only_whole_pages_inside_the_range )
{
    if ( !can_populate() )
        GTEST_SKIP() << "populate() does nothing here";
    block b;
    ASSERT_NE( b.address, nullptr );
    auto * const first{ static_cast<std::byte *>( b.address ) };
    // [ page 1 + 1 byte, page 4 ): the whole pages in it are 2 and 3
    EXPECT_TRUE( populate( first + page_size + 1, 3 * page_size - 1 ) );
    auto const at{ [ & ]( std::size_t const page ) { return resident_pages( as_bytes( first + page * page_size, page_size ) ).value_or( 99 ); } };
    EXPECT_EQ( at( 1 ), 0U ) << "the page the range starts inside of";
    EXPECT_EQ( at( 2 ), 1U );
    EXPECT_EQ( at( 3 ), 1U );
    EXPECT_EQ( at( 4 ), 0U ) << "the page the range ends at";
    EXPECT_EQ( resident_pages( as_bytes( first, bytes ) ).value_or( 0 ), 2U );
}

TEST( populate, a_range_without_a_whole_page_is_trivially_done )
{
    block b;
    ASSERT_NE( b.address, nullptr );
    auto * const first{ static_cast<std::byte *>( b.address ) };
    EXPECT_TRUE( populate( first, 0 ) );
    EXPECT_TRUE( populate( first + 1, page_size ) ) << "less than a page inside the range";
    EXPECT_EQ( resident_pages( as_bytes( first, bytes ) ).value_or( 0 ), 0U );
}

TEST( populate, reports_a_hole_in_the_range )
{
    if ( !can_populate() )
        GTEST_SKIP() << "populate() does nothing here";
    block b;
    ASSERT_NE( b.address, nullptr );
    auto * const first{ static_cast<std::byte *>( b.address ) };
    // a hole at page 2 (munmap of part of a block is fine, and so is freeing the whole of it later)
    free( first + 2 * page_size, page_size );
    EXPECT_FALSE( populate( first, bytes ) );
}

TEST( populate, vector_spare_capacity )
{
    heap_vector<std::uint64_t> heap;
    heap.reserve( 1 << 20 ); // 8 MiB
    vm_vector<std::uint64_t, std::uint32_t> mapped;
    ASSERT_TRUE( static_cast<bool>( mapped.map_memory( 1 << 20 ) ) );
    mapped.resize( 100 );

    auto const check{ []( auto & vec ) {
        auto const done{ vec.populate_spare_capacity() };
        auto const begin{ reinterpret_cast<std::uintptr_t>( vec.data() + vec.size() ) };
        auto const end  { reinterpret_cast<std::uintptr_t>( vec.data() + vec.capacity() ) };
        auto const first_page{ ( begin + page_size - 1 ) & ~std::uintptr_t{ page_size - 1 } };
        auto const last_page { end & ~std::uintptr_t{ page_size - 1 } };
        auto const whole{ ( last_page - first_page ) / page_size };
        auto const resident{ resident_pages( as_bytes( reinterpret_cast<void const *>( first_page ), last_page - first_page ) ) };
        if ( !resident )
            return; // (residency cannot be asked here)
        if ( can_populate() )
        {
            EXPECT_TRUE( done );
            EXPECT_EQ( *resident, whole );
        }
        else
            EXPECT_FALSE( done );
    } };
    check( heap );
    check( mapped );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
