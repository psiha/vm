// Tests for psi::vm::strided_view — the transverse slice of an array of
// fixed-stride entries.
//
// Two shapes are covered: the lane of a strided_vector (uniform element type,
// stride counted in elements) and a hand-built interleaved record array whose
// fields differ in type and width (stride counted in bytes), which is what the
// byte-offset constructor addresses.

#include <psi/vm/containers/strided_view.hpp>
#include <psi/vm/containers/strided_vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <ranges>
#include <span>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

TEST( StridedView, a_default_constructed_span_is_an_empty_range )
{
    strided_view<std::uint32_t const> const empty;
    EXPECT_TRUE ( empty.empty()             );
    EXPECT_EQ   ( empty.size (), 0          );
    EXPECT_EQ   ( empty.begin(), empty.end());
    EXPECT_TRUE ( std::ranges::empty( empty ) );
}

TEST( StridedView, a_lane_of_a_strided_vector_visits_one_element_of_every_entry )
{
    // Three xyz entries: (0,1,2) (3,4,5) (6,7,8).
    strided_vector<std::uint32_t> points( 3, 3 ); // ( stride, count )
    for ( std::uint32_t entry{ 0 }; entry < 3; ++entry ) {
        for ( std::uint32_t lane{ 0 }; lane < 3; ++lane ) {
            points[ entry ][ lane ] = entry * 3 + lane;
        }
    }

    auto const xs{ points.field( 0 ) };
    auto const ys{ points.field( 1 ) };
    auto const zs{ points.field( 2 ) };

    EXPECT_EQ( xs.size(), 3 );
    EXPECT_EQ( xs.strideBytes(), 3 * sizeof( std::uint32_t ) );
    EXPECT_FALSE( xs.contiguous() );

    EXPECT_TRUE( std::ranges::equal( xs, std::array<std::uint32_t, 3>{ 0, 3, 6 } ) );
    EXPECT_TRUE( std::ranges::equal( ys, std::array<std::uint32_t, 3>{ 1, 4, 7 } ) );
    EXPECT_TRUE( std::ranges::equal( zs, std::array<std::uint32_t, 3>{ 2, 5, 8 } ) );

    EXPECT_EQ( ys.front(), 1 );
    EXPECT_EQ( ys.back (), 7 );
    EXPECT_EQ( ys[ 1 ]   , 4 );
}

TEST( StridedView, a_mutable_lane_writes_through_to_the_entries )
{
    strided_vector<std::uint32_t> points( 2, 2 ); // ( stride, count )
    points[ 0 ][ 0 ] = 1; points[ 0 ][ 1 ] = 2;
    points[ 1 ][ 0 ] = 3; points[ 1 ][ 1 ] = 4;

    for ( auto & x : points.field( 0 ) ) {
        x *= 10;
    }

    EXPECT_EQ( points[ 0 ][ 0 ], 10 );
    EXPECT_EQ( points[ 0 ][ 1 ],  2 );
    EXPECT_EQ( points[ 1 ][ 0 ], 30 );
    EXPECT_EQ( points[ 1 ][ 1 ],  4 );
}

TEST( StridedView, the_fields_of_an_interleaved_record_carry_their_own_types )
{
    // A row-major record: { u16 a; u8 b; u32 c; } laid out by hand with no
    // padding - each field is one span of its own type at its own offset.
    static constexpr std::size_t stride  { sizeof( std::uint16_t ) + sizeof( std::uint8_t ) + sizeof( std::uint32_t ) };
    static constexpr std::size_t offsetA { 0 };
    static constexpr std::size_t offsetB { offsetA + sizeof( std::uint16_t ) };
    static constexpr std::size_t offsetC { offsetB + sizeof( std::uint8_t  ) };
    static constexpr std::uint32_t rows  { 4 };

    std::vector<std::byte> storage( stride * rows );
    auto * const base{ storage.data() };

    strided_view<std::uint16_t> const as{ base, offsetA, stride, rows };
    strided_view<std::uint8_t > const bs{ base, offsetB, stride, rows };
    strided_view<std::uint32_t> const cs{ base, offsetC, stride, rows };

    for ( std::uint32_t row{ 0 }; row < rows; ++row ) {
        as[ row ] = static_cast<std::uint16_t>( 1000 + row );
        bs[ row ] = static_cast<std::uint8_t >(    7 + row );
        cs[ row ] = 100'000 + row;
    }

    // Every field reads back its own values, undisturbed by its neighbours.
    EXPECT_TRUE( std::ranges::equal( as, std::array<std::uint16_t, 4>{ 1000, 1001, 1002, 1003 } ) );
    EXPECT_TRUE( std::ranges::equal( bs, std::array<std::uint8_t , 4>{    7,    8,    9,   10 } ) );
    EXPECT_TRUE( std::ranges::equal( cs, std::array<std::uint32_t, 4>{ 100'000, 100'001, 100'002, 100'003 } ) );

    // ...and each is where the record layout says it is.
    EXPECT_EQ( as.base(), base + offsetA );
    EXPECT_EQ( bs.base(), base + offsetB );
    EXPECT_EQ( cs.base(), base + offsetC );
}

TEST( StridedView, a_stride_of_one_element_is_a_contiguous_span )
{
    std::array<std::uint32_t, 5> values{ 5, 4, 3, 2, 1 };
    strided_view<std::uint32_t> const all{ values.data(), sizeof( std::uint32_t ), static_cast<std::uint32_t>( values.size() ) };

    EXPECT_TRUE( all.contiguous()      );
    EXPECT_EQ  ( all.data(), values.data() );
    EXPECT_TRUE( std::ranges::equal( all, values ) );
}

TEST( StridedView, the_iterator_is_random_access_so_algorithms_run_over_a_field )
{
    strided_vector<std::uint32_t> points( 2, 5 ); // ( stride, count )
    std::array<std::uint32_t, 5> const xs{ 40, 10, 50, 20, 30 };
    for ( std::uint32_t entry{ 0 }; entry < 5; ++entry ) {
        points[ entry ][ 0 ] = xs[ entry ];
        points[ entry ][ 1 ] = 0xDEAD; // the neighbouring lane must stay untouched
    }

    auto const lane{ points.field( 0 ) };
    static_assert( std::random_access_iterator<decltype( lane.begin() )> );

    EXPECT_EQ( std::ranges::max( lane ), 50 );
    EXPECT_EQ( std::ranges::min( lane ), 10 );
    EXPECT_EQ( std::accumulate( lane.begin(), lane.end(), std::uint32_t{ 0 } ), 150 );
    EXPECT_EQ( lane.end() - lane.begin(), 5 );
    EXPECT_EQ( *( lane.begin() + 3 ), 20 );

    auto const found{ std::ranges::find( lane, 50 ) };
    EXPECT_EQ( found - lane.begin(), 2 );

    // Sorting the lane would permute fields independently of their entries,
    // which is what strided_vector's own iterator is for - but reading a
    // sorted copy out of one is exactly what a comparator does.
    std::vector<std::uint32_t> copy( lane.begin(), lane.end() );
    std::ranges::sort( copy );
    EXPECT_TRUE( std::ranges::equal( copy, std::array<std::uint32_t, 5>{ 10, 20, 30, 40, 50 } ) );

    EXPECT_EQ( points[ 0 ][ 1 ], 0xDEAD );
}

TEST( StridedView, a_mutable_span_converts_to_a_const_one )
{
    std::array<std::uint32_t, 3> values{ 1, 2, 3 };
    strided_view<std::uint32_t      > const mutableSpan{ values.data(), sizeof( std::uint32_t ), 3 };
    strided_view<std::uint32_t const> const constSpan  { mutableSpan };

    EXPECT_TRUE( std::ranges::equal( constSpan, values ) );
    EXPECT_EQ  ( constSpan.size(), mutableSpan.size() );

    // The iterators convert too.
    strided_view<std::uint32_t const>::iterator const it{ mutableSpan.begin() };
    EXPECT_EQ( *it, 1 );
}

TEST( StridedView, as_reinterprets_the_elements_as_a_same_sized_type )
{
    enum class Id : std::uint32_t {};

    strided_vector<std::uint32_t> points( 2, 2 ); // ( stride, count )
    points[ 0 ][ 0 ] = 11;
    points[ 1 ][ 0 ] = 22;

    auto const ids{ points.field( 0 ).as<Id const>() };
    EXPECT_EQ( ids[ 0 ], Id{ 11 } );
    EXPECT_EQ( ids[ 1 ], Id{ 22 } );
    EXPECT_EQ( ids.strideBytes(), points.field( 0 ).strideBytes() );
}

TEST( StridedView, the_iterators_outlive_the_span_that_made_them )
{
    // enable_borrowed_range: a range adaptor may keep the iterators of a
    // temporary span.
    static_assert( std::ranges::borrowed_range<strided_view<std::uint32_t const>> );
    static_assert( std::ranges::view          <strided_view<std::uint32_t const>> );

    strided_vector<std::uint32_t> points( 2, 3 ); // ( stride, count )
    points[ 0 ][ 0 ] = 1; points[ 1 ][ 0 ] = 2; points[ 2 ][ 0 ] = 3;

    auto const doubled{ points.field( 0 ) | std::views::transform( []( auto const v ) { return v * 2; } ) };
    EXPECT_TRUE( std::ranges::equal( doubled, std::array<std::uint32_t, 3>{ 2, 4, 6 } ) );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
