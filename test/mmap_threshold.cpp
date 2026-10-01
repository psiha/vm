////////////////////////////////////////////////////////////////////////////////
///
/// \file mmap_threshold.cpp
/// ------------------------
///
/// Tests for mmap_threshold_storage: heap_storage below the threshold,
/// vm_storage from it up, and the switch between them both ways.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include <psi/vm/containers/storage/mmap_threshold.hpp>
#include <psi/vm/containers/heap_vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace
{
    std::size_t constexpr threshold{ 128 * 1024 };
    using vec_t = mmap_threshold_vector<std::uint32_t, std::uint32_t, threshold>;
    std::uint32_t constexpr below{ threshold / sizeof( std::uint32_t ) - 1 };

    std::size_t capacity_bytes( vec_t const & vec ) { return std::size_t{ vec.capacity() } * sizeof( std::uint32_t ); }
    bool holds_iota( vec_t const & vec, std::uint32_t const count ) { return ( vec.size() == count ) && std::ranges::equal( vec, std::views::iota( 0U, count ) ); }
} // anonymous namespace

TEST( mmap_threshold_storage, a_small_vector_is_a_heap_block )
{
    vec_t vec;
    EXPECT_TRUE( vec.empty() );
    EXPECT_FALSE( vec.is_mapped() );
    for ( std::uint32_t i{ 0 }; i < 1000; ++i )
        vec.push_back( i );
    EXPECT_FALSE( vec.is_mapped() );
    EXPECT_TRUE( holds_iota( vec, 1000 ) );
}

TEST( mmap_threshold_storage, crosses_the_threshold_both_ways )
{
    vec_t vec;
    vec.reserve( below );
    EXPECT_FALSE( vec.is_mapped() );
    std::uint32_t i{ 0 };
    for ( ; i < below; ++i )
        vec.push_back( i );
    EXPECT_FALSE( vec.is_mapped() );

    for ( ; i < 64 * below; ++i ) // into a mapping, then grown by remapping
        vec.push_back( i );
    EXPECT_TRUE ( vec.is_mapped() );
    EXPECT_GE   ( capacity_bytes( vec ), threshold );
    EXPECT_TRUE ( holds_iota( vec, 64 * below ) );

    vec.resize( 8 * below ); // stays mapped: the mapping shrinks
    EXPECT_GE( capacity_bytes( vec ), 64 * below * sizeof( std::uint32_t ) ) << "resize() down released capacity";
    vec.shrink_to_fit();
    EXPECT_TRUE( vec.is_mapped() );
    EXPECT_LT  ( capacity_bytes( vec ), 9 * below * sizeof( std::uint32_t ) );
    EXPECT_TRUE( holds_iota( vec, 8 * below ) );

    vec.resize( 1000 ); // below the threshold: still a mapping, shrunk
    vec.shrink_to_fit();
    EXPECT_TRUE( vec.is_mapped() );
    EXPECT_LT  ( capacity_bytes( vec ), threshold );
    EXPECT_TRUE( holds_iota( vec, 1000 ) );

    for ( i = 1000; i < 2 * below; ++i ) // and grows as one
        vec.push_back( i );
    EXPECT_TRUE( vec.is_mapped() );
    EXPECT_TRUE( holds_iota( vec, 2 * below ) );

    vec.clear();
    vec.shrink_to_fit();
    EXPECT_TRUE( vec.is_mapped() );
    EXPECT_TRUE( vec.empty() );
}

// Only freeing ends a mapping.
TEST( mmap_threshold_storage, a_mapping_lasts_until_freed )
{
    vec_t vec;
    vec.grow_by_amortized( 2 * below, no_init );
    ASSERT_TRUE( vec.is_mapped() );
    vec = vec_t{};
    EXPECT_FALSE( vec.is_mapped() );
    EXPECT_EQ   ( vec.capacity(), 0U );
}

// Declared trivially moveable, so a container of them relocates them by
// realloc (heap_storage's trivially moveable growth path), contents intact.
TEST( mmap_threshold_storage, a_container_of_them_relocates_them_bitwise )
{
    static_assert( is_trivially_moveable<vec_t> );
    heap_vector<vec_t> outer;
    for ( std::uint32_t v{ 0 }; v < 64; ++v )
    {
        auto & inner{ outer.emplace_back() };
        for ( std::uint32_t i{ 0 }; i < ( v % 2 ? 4 * below : 100U ); ++i ) // mapped and heap ones
            inner.push_back( i );
    }
    for ( std::uint32_t v{ 0 }; v < 64; ++v )
    {
        EXPECT_EQ  ( outer[ v ].is_mapped(), v % 2 == 1 );
        EXPECT_TRUE( holds_iota( outer[ v ], v % 2 ? 4 * below : 100U ) );
        outer[ v ].push_back( outer[ v ].size() ); // the relocated cache still describes its storage
    }
}

// A first growth past the threshold is one mapping of the target, not a heap
// block grown right after.
TEST( mmap_threshold_storage, a_first_large_growth_maps_directly )
{
    vec_t vec;
    vec.grow_by_amortized( 4 * below, no_init );
    EXPECT_TRUE( vec.is_mapped() );
    EXPECT_GE  ( vec.capacity(), 4 * below );
    std::ranges::copy( std::views::iota( 0U, 4 * below ), vec.begin() );
    EXPECT_TRUE( holds_iota( vec, 4 * below ) );
}

TEST( mmap_threshold_storage, copies_moves_and_swaps )
{
    vec_t small, large;
    for ( std::uint32_t i{ 0 }; i < 100; ++i )
        small.push_back( i );
    for ( std::uint32_t i{ 0 }; i < 4 * below; ++i )
        large.push_back( i );
    ASSERT_FALSE( small.is_mapped() );
    ASSERT_TRUE ( large.is_mapped() );

    vec_t const large_copy{ large };
    EXPECT_TRUE( large_copy.is_mapped() );
    EXPECT_TRUE( holds_iota( large_copy, 4 * below ) );

    small.swap( large );
    EXPECT_TRUE( holds_iota( small, 4 * below ) );
    EXPECT_TRUE( holds_iota( large, 100 ) );
    EXPECT_TRUE ( small.is_mapped() );
    EXPECT_FALSE( large.is_mapped() );

    vec_t moved{ std::move( small ) };
    EXPECT_TRUE( holds_iota( moved, 4 * below ) );
    EXPECT_TRUE( small.empty() );
    small = std::move( large );
    EXPECT_TRUE( holds_iota( small, 100 ) );
    small.push_back( 100 ); // the moved-to storage is in sync with its cache
    EXPECT_TRUE( holds_iota( small, 101 ) );
    moved.push_back( 4 * below );
    EXPECT_TRUE( holds_iota( moved, 4 * below + 1 ) );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
