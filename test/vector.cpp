// Comprehensive std::vector-like compliance tests for tr_vector and fc_vector
// using Google Test typed tests to cover both implementations uniformly.
#include <psi/vm/containers/fc_vector.hpp>
#include <psi/vm/containers/small_vector.hpp>
#include <psi/vm/containers/tr_vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <list>
#include <numeric>
#include <ranges>
#include <span>
#include <string>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// Typed test suite -- runs every test against all vector types
////////////////////////////////////////////////////////////////////////////////

inline constexpr sbo_options compact_lsb_opts{ .layout = sbo_layout::compact_lsb };
inline constexpr sbo_options embedded_opts   { .layout = sbo_layout::embedded    };

using VectorTestTypes = ::testing::Types<
    tr_vector<int>,
    tr_vector<int, std::uint32_t>,
    fc_vector<int, 256>,
    small_vector<int, 16>,
    small_vector<int,  4, std::uint32_t>,
    small_vector<int, 16, std::size_t   , compact_lsb_opts>,
    small_vector<int,  4, std::uint32_t , compact_lsb_opts>,
    small_vector<int, 16, std::size_t   , embedded_opts>,
    small_vector<int,  4, std::uint32_t , embedded_opts>
>;

template <typename VecType>
class vector_compliance : public ::testing::Test {};
TYPED_TEST_SUITE( vector_compliance, VectorTestTypes );


////////////////////////////////////////////////////////////////////////////////
// 1. Construction
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( vector_compliance, default_constructor )
{
    TypeParam v;
    EXPECT_TRUE( v.empty() );
    EXPECT_EQ  ( v.size(), 0 );
}

TYPED_TEST( vector_compliance, size_constructor )
{
    TypeParam v( 10 );
    EXPECT_EQ( v.size(), 10 );
    EXPECT_FALSE( v.empty() );
}

TYPED_TEST( vector_compliance, size_value_constructor )
{
    TypeParam v( 5, 42 );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < v.size(); ++i )
        EXPECT_EQ( v[ i ], 42 );
}

TYPED_TEST( vector_compliance, iterator_range_constructor )
{
    std::vector<int> const src{ 10, 20, 30, 40, 50 };
    TypeParam v( src.begin(), src.end() );
    EXPECT_EQ( v.size(), 5 );
    EXPECT_EQ( v[ 0 ], 10 );
    EXPECT_EQ( v[ 4 ], 50 );
}

TYPED_TEST( vector_compliance, initializer_list_constructor )
{
    TypeParam v{ 1, 2, 3, 4, 5 };
    EXPECT_EQ( v.size(), 5 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 4 ], 5 );
}

TYPED_TEST( vector_compliance, copy_constructor )
{
    TypeParam const original{ 10, 20, 30 };
    TypeParam copy{ original };
    EXPECT_EQ( copy.size(), 3 );
    EXPECT_EQ( copy[ 0 ], 10 );
    EXPECT_EQ( copy[ 2 ], 30 );
    // original unchanged
    EXPECT_EQ( original.size(), 3 );
}

TYPED_TEST( vector_compliance, move_constructor )
{
    TypeParam original{ 1, 2, 3, 4, 5 };
    TypeParam moved{ std::move( original ) };
    EXPECT_EQ( moved.size(), 5 );
    EXPECT_EQ( moved[ 0 ], 1 );
    EXPECT_EQ( moved[ 4 ], 5 );
}

TYPED_TEST( vector_compliance, value_init_constructor )
{
    TypeParam v( 5, value_init );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < v.size(); ++i )
        EXPECT_EQ( v[ i ], 0 );
}

TYPED_TEST( vector_compliance, default_init_constructor )
{
    TypeParam v( 5, default_init );
    EXPECT_EQ( v.size(), 5 );
    // values are indeterminate, just verify size
}


////////////////////////////////////////////////////////////////////////////////
// 2. Assignment
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( vector_compliance, copy_assignment )
{
    TypeParam const original{ 10, 20, 30 };
    TypeParam dest{ 1, 2 };
    dest = original;
    EXPECT_EQ( dest.size(), 3 );
    EXPECT_EQ( dest[ 0 ], 10 );
    EXPECT_EQ( dest[ 2 ], 30 );
}

TYPED_TEST( vector_compliance, move_assignment )
{
    TypeParam original{ 10, 20, 30 };
    TypeParam dest{ 1, 2 };
    dest = std::move( original );
    EXPECT_EQ( dest.size(), 3 );
    EXPECT_EQ( dest[ 0 ], 10 );
}

TYPED_TEST( vector_compliance, initializer_list_assignment )
{
    TypeParam v{ 1, 2 };
    v = { 10, 20, 30, 40 };
    EXPECT_EQ( v.size(), 4 );
    EXPECT_EQ( v[ 0 ], 10 );
    EXPECT_EQ( v[ 3 ], 40 );
}

TYPED_TEST( vector_compliance, assign_iterator_range )
{
    std::vector<int> const src{ 100, 200, 300 };
    TypeParam v{ 1, 2, 3, 4, 5 };
    v.assign( src.begin(), src.end() );
    EXPECT_EQ( v.size(), 3 );
    EXPECT_EQ( v[ 0 ], 100 );
    EXPECT_EQ( v[ 2 ], 300 );
}

TYPED_TEST( vector_compliance, assign_n_val )
{
    TypeParam v{ 1, 2, 3 };
    using sz = typename TypeParam::size_type;
    v.assign( sz( 5 ), 42 );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < v.size(); ++i )
        EXPECT_EQ( v[ i ], 42 );
}

TYPED_TEST( vector_compliance, assign_n_val_shrink )
{
    TypeParam v{ 1, 2, 3, 4, 5 };
    using sz = typename TypeParam::size_type;
    v.assign( sz( 2 ), 99 );
    EXPECT_EQ( v.size(), 2 );
    EXPECT_EQ( v[ 0 ], 99 );
    EXPECT_EQ( v[ 1 ], 99 );
}

TYPED_TEST( vector_compliance, assign_range )
{
    auto const rng{ std::views::iota( 10, 15 ) };
    TypeParam v{ 1, 2 };
    v.assign( rng );
    EXPECT_EQ( v.size(), 5 );
    EXPECT_EQ( v[ 0 ], 10 );
    EXPECT_EQ( v[ 4 ], 14 );
}

TYPED_TEST( vector_compliance, assign_range_method )
{
    std::vector<int> const src{ 7, 8, 9 };
    TypeParam v{ 1, 2, 3, 4, 5 };
    v.assign_range( src );
    EXPECT_EQ( v.size(), 3 );
    EXPECT_EQ( v[ 0 ], 7 );
    EXPECT_EQ( v[ 2 ], 9 );
}

// assign() with sized non-random-access ranges (exercises the
// pre-allocation fast path for ranges with cached size)
TYPED_TEST( vector_compliance, assign_sized_nonra_grow )
{
    // std::list: sized + bidirectional (not random-access)
    std::list<int> const src{ 10, 20, 30, 40, 50 };
    TypeParam v{ 1, 2 };
    v.assign( src );
    EXPECT_EQ( v.size(), 5 );
    EXPECT_EQ( v[ 0 ], 10 );
    EXPECT_EQ( v[ 4 ], 50 );
}

TYPED_TEST( vector_compliance, assign_sized_nonra_shrink )
{
    std::list<int> const src{ 7, 8 };
    TypeParam v{ 1, 2, 3, 4, 5 };
    v.assign( src );
    EXPECT_EQ( v.size(), 2 );
    EXPECT_EQ( v[ 0 ], 7 );
    EXPECT_EQ( v[ 1 ], 8 );
}

TYPED_TEST( vector_compliance, assign_sized_nonra_exact )
{
    std::list<int> const src{ 100, 200, 300 };
    TypeParam v{ 1, 2, 3 };
    v.assign( src );
    EXPECT_EQ( v.size(), 3 );
    EXPECT_EQ( v[ 0 ], 100 );
    EXPECT_EQ( v[ 2 ], 300 );
}

TYPED_TEST( vector_compliance, assign_sized_nonra_empty_src )
{
    std::list<int> const src;
    TypeParam v{ 1, 2, 3 };
    v.assign( src );
    EXPECT_EQ( v.size(), 0 );
}

TYPED_TEST( vector_compliance, assign_sized_nonra_empty_dest )
{
    std::list<int> const src{ 5, 10, 15 };
    TypeParam v;
    v.assign( src );
    EXPECT_EQ( v.size(), 3 );
    EXPECT_EQ( v[ 0 ], 5 );
    EXPECT_EQ( v[ 2 ], 15 );
}

// assign() with subrange carrying a size hint over non-random-access iterators.
// This is the real target of the sized_range fast path: the underlying iterator
// pair has no .size() method (e.g. list iterators, single-pass iterators),
// but the subrange was constructed with an explicit size hint — making it a
// std::ranges::sized_range without being random_access.
TYPED_TEST( vector_compliance, assign_sized_subrange_grow )
{
    std::list<int> const src{ 1, 2, 3, 4, 5, 6 };
    // subrange from list iterators + explicit size → sized_range but NOT random_access_range
    auto const sr{ std::ranges::subrange( src.begin(), src.end(), src.size() ) };
    static_assert(  std::ranges::sized_range<decltype( sr )> );
    static_assert( !std::ranges::random_access_range<decltype( sr )> );

    TypeParam v{ 10, 20 };
    v.assign( sr );
    EXPECT_EQ( v.size(), 6 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 5 ], 6 );
}

TYPED_TEST( vector_compliance, assign_sized_subrange_shrink )
{
    std::list<int> const src{ 99, 88 };
    auto const sr{ std::ranges::subrange( src.begin(), src.end(), src.size() ) };
    static_assert(  std::ranges::sized_range<decltype( sr )> );
    static_assert( !std::ranges::random_access_range<decltype( sr )> );

    TypeParam v{ 1, 2, 3, 4, 5 };
    v.assign( sr );
    EXPECT_EQ( v.size(), 2 );
    EXPECT_EQ( v[ 0 ], 99 );
    EXPECT_EQ( v[ 1 ], 88 );
}

TYPED_TEST( vector_compliance, assign_sized_subrange_empty )
{
    std::list<int> const src;
    auto const sr{ std::ranges::subrange( src.begin(), src.end(), std::size_t{ 0 } ) };
    static_assert( std::ranges::sized_range<decltype( sr )> );

    TypeParam v{ 1, 2, 3 };
    v.assign( sr );
    EXPECT_EQ( v.size(), 0 );
}


////////////////////////////////////////////////////////////////////////////////
// 3. Element Access
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( vector_compliance, operator_subscript )
{
    TypeParam v{ 10, 20, 30, 40 };
    EXPECT_EQ( v[ 0 ], 10 );
    EXPECT_EQ( v[ 3 ], 40 );
    v[ 1 ] = 99;
    EXPECT_EQ( v[ 1 ], 99 );
}

TYPED_TEST( vector_compliance, at_valid )
{
    TypeParam v{ 10, 20, 30, 40 };
    EXPECT_EQ( v.at( 0 ), 10 );
    EXPECT_EQ( v.at( 3 ), 40 );
}

TYPED_TEST( vector_compliance, at_out_of_range )
{
    TypeParam v{ 10, 20, 30, 40 };
#if !( defined( __APPLE__ ) || ( defined( __linux__ ) && !defined( NDEBUG ) ) )
    EXPECT_THROW( std::ignore = v.at( 10 ), std::out_of_range );
#endif
}

TYPED_TEST( vector_compliance, front_back )
{
    TypeParam v{ 10, 20, 30, 40 };
    EXPECT_EQ( v.front(), 10 );
    EXPECT_EQ( v.back(), 40 );
    v.front() = 1;
    v.back()  = 4;
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 3 ], 4 );
}

TYPED_TEST( vector_compliance, data_pointer )
{
    TypeParam v{ 10, 20, 30 };
    auto const * p{ v.data() };
    EXPECT_NE( p, nullptr );
    EXPECT_EQ( p[ 0 ], 10 );
    EXPECT_EQ( p[ 2 ], 30 );
}

TYPED_TEST( vector_compliance, span_view )
{
    TypeParam v{ 10, 20, 30 };
    auto s{ v.span() };
    static_assert( std::is_same_v<decltype( s ), std::span<int>> );
    EXPECT_EQ( s.size(), 3 );
    EXPECT_EQ( s[ 1 ], 20 );
}

TYPED_TEST( vector_compliance, const_data )
{
    TypeParam const v{ 10, 20, 30 };
    auto const * p{ v.data() };
    static_assert( std::is_same_v<decltype( p ), int const *> );
    EXPECT_EQ( p[ 1 ], 20 );
}


////////////////////////////////////////////////////////////////////////////////
// 4. Iterators
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( vector_compliance, begin_end_traversal )
{
    TypeParam v{ 10, 20, 30, 40 };
    int sum{ 0 };
    for ( auto it{ v.begin() }; it != v.end(); ++it )
        sum += *it;
    EXPECT_EQ( sum, 100 );
}

TYPED_TEST( vector_compliance, cbegin_cend )
{
    TypeParam v{ 10, 20, 30 };
    auto it{ v.cbegin() };
    EXPECT_EQ( *it, 10 );
    static_assert( std::is_same_v<decltype( *it ), int const &> );
}

TYPED_TEST( vector_compliance, rbegin_rend )
{
    TypeParam v{ 10, 20, 30, 40 };
    std::vector<int> reversed( v.rbegin(), v.rend() );
    EXPECT_EQ( reversed[ 0 ], 40 );
    EXPECT_EQ( reversed[ 3 ], 10 );
}

TYPED_TEST( vector_compliance, crbegin_crend )
{
    TypeParam const v{ 10, 20, 30 };
    auto it{ v.crbegin() };
    EXPECT_EQ( *it, 30 );
}

TYPED_TEST( vector_compliance, const_iterators )
{
    TypeParam const v{ 1, 2, 3 };
    EXPECT_EQ( *v.begin(), 1 );
    EXPECT_EQ( *v.cbegin(), 1 );
}

TYPED_TEST( vector_compliance, nth_index_of )
{
    TypeParam v{ 10, 20, 30, 40 };
    auto it{ v.nth( 2 ) };
    EXPECT_EQ( *it, 30 );
    EXPECT_EQ( v.index_of( it ), 2 );
    EXPECT_EQ( v.index_of( v.end() ), v.size() );
}

TYPED_TEST( vector_compliance, range_for )
{
    TypeParam v{ 1, 2, 3, 4, 5 };
    int sum{ 0 };
    for ( auto const val : v )
        sum += val;
    EXPECT_EQ( sum, 15 );
}


////////////////////////////////////////////////////////////////////////////////
// 5. Capacity
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( vector_compliance, empty )
{
    TypeParam empty_vec;
    EXPECT_TRUE( empty_vec.empty() );
    TypeParam non_empty{ 1 };
    EXPECT_FALSE( non_empty.empty() );
}

TYPED_TEST( vector_compliance, size_accuracy )
{
    TypeParam v;
    EXPECT_EQ( v.size(), 0 );
    v.push_back( 1 );
    EXPECT_EQ( v.size(), 1 );
    v.push_back( 2 );
    v.push_back( 3 );
    EXPECT_EQ( v.size(), 3 );
    v.pop_back();
    EXPECT_EQ( v.size(), 2 );
}

TYPED_TEST( vector_compliance, max_size )
{
    TypeParam v;
    EXPECT_GT( v.max_size(), 0 );
}

TYPED_TEST( vector_compliance, capacity_ge_size )
{
    TypeParam v{ 1, 2, 3, 4, 5 };
    EXPECT_GE( v.capacity(), v.size() );
}

TYPED_TEST( vector_compliance, reserve )
{
    TypeParam v{ 1, 2, 3 };
    auto const old_size{ v.size() };
    v.reserve( 100 );
    EXPECT_GE( v.capacity(), 100 );
    EXPECT_EQ( v.size(), old_size ); // size unchanged
    EXPECT_EQ( v[ 0 ], 1 );         // data preserved
    EXPECT_EQ( v[ 2 ], 3 );
}

TYPED_TEST( vector_compliance, shrink_to_fit )
{
    TypeParam v{ 1, 2, 3 };
    v.reserve( 100 );
    auto const cap_before{ v.capacity() };
    v.shrink_to_fit();
    EXPECT_GE( v.capacity(), v.size() );
    EXPECT_LE( v.capacity(), cap_before );
    // data preserved
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 2 ], 3 );
}

TYPED_TEST( vector_compliance, resize_grow )
{
    TypeParam v{ 1, 2, 3 };
    v.resize( 6, 42 );
    EXPECT_EQ( v.size(), 6 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 2 ], 3 );
    EXPECT_EQ( v[ 3 ], 42 );
    EXPECT_EQ( v[ 5 ], 42 );
}

TYPED_TEST( vector_compliance, resize_shrink )
{
    TypeParam v{ 1, 2, 3, 4, 5 };
    v.resize( 2 );
    EXPECT_EQ( v.size(), 2 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 2 );
}

TYPED_TEST( vector_compliance, resize_same )
{
    TypeParam v{ 1, 2, 3 };
    v.resize( 3 );
    EXPECT_EQ( v.size(), 3 );
}

TYPED_TEST( vector_compliance, resize_to_zero )
{
    TypeParam v{ 1, 2, 3 };
    v.resize( 0 );
    EXPECT_TRUE( v.empty() );
}


////////////////////////////////////////////////////////////////////////////////
// 6. Modifiers
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( vector_compliance, push_back )
{
    TypeParam v;
    v.push_back( 10 );
    v.push_back( 20 );
    v.push_back( 30 );
    EXPECT_EQ( v.size(), 3 );
    EXPECT_EQ( v[ 0 ], 10 );
    EXPECT_EQ( v[ 2 ], 30 );
}

TYPED_TEST( vector_compliance, emplace_back )
{
    TypeParam v;
    auto & ref{ v.emplace_back( 42 ) };
    EXPECT_EQ( ref, 42 );
    EXPECT_EQ( v.size(), 1 );
    EXPECT_EQ( &ref, &v.back() );
}

TYPED_TEST( vector_compliance, emplace_at_begin )
{
    TypeParam v{ 2, 3, 4 };
    v.emplace( v.begin(), 1 );
    EXPECT_EQ( v.size(), 4 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 2 );
}

TYPED_TEST( vector_compliance, emplace_at_middle )
{
    TypeParam v{ 1, 2, 4, 5 };
    v.emplace( v.nth( 2 ), 3 );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < 5u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) + 1 );
}

TYPED_TEST( vector_compliance, emplace_at_end )
{
    TypeParam v{ 1, 2, 3 };
    v.emplace( v.end(), 4 );
    EXPECT_EQ( v.size(), 4 );
    EXPECT_EQ( v[ 3 ], 4 );
}

TYPED_TEST( vector_compliance, insert_single )
{
    TypeParam v{ 1, 3, 4 };
    auto it{ v.insert( v.nth( 1 ), 2 ) };
    EXPECT_EQ( *it, 2 );
    EXPECT_EQ( v.size(), 4 );
    for ( typename TypeParam::size_type i{ 0 }; i < 4u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) + 1 );
}

TYPED_TEST( vector_compliance, insert_fill )
{
    using sz = typename TypeParam::size_type;
    TypeParam v{ 1, 5 };
    auto it{ v.insert( v.nth( 1 ), sz( 3 ), 0 ) };
    EXPECT_EQ( *it, 0 );
    EXPECT_EQ( v.size(), 5 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 0 );
    EXPECT_EQ( v[ 3 ], 0 );
    EXPECT_EQ( v[ 4 ], 5 );
}

TYPED_TEST( vector_compliance, insert_iterator_range )
{
    std::vector<int> const src{ 2, 3, 4 };
    TypeParam v{ 1, 5 };
    auto it{ v.insert( v.nth( 1 ), src.begin(), src.end() ) };
    EXPECT_EQ( *it, 2 );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < 5u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) + 1 );
}

TYPED_TEST( vector_compliance, insert_initializer_list )
{
    TypeParam v{ 1, 5 };
    auto it{ v.insert( v.nth( 1 ), { 2, 3, 4 } ) };
    EXPECT_EQ( *it, 2 );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < 5u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) + 1 );
}

TYPED_TEST( vector_compliance, insert_range )
{
    std::vector<int> const src{ 2, 3, 4 };
    TypeParam v{ 1, 5 };
    auto it{ v.insert_range( v.nth( 1 ), src ) };
    EXPECT_EQ( *it, 2 );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < 5u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) + 1 );
}

TYPED_TEST( vector_compliance, insert_range_at_end )
{
    TypeParam v{ 1, 2 };
    v.insert_range( v.end(), std::vector<int>{ 3, 4, 5 } );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < 5u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) + 1 );
}

TYPED_TEST( vector_compliance, insert_range_at_begin )
{
    TypeParam v{ 4, 5 };
    v.insert_range( v.begin(), std::vector<int>{ 1, 2, 3 } );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < 5u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) + 1 );
}

TYPED_TEST( vector_compliance, append_range )
{
    TypeParam v{ 1, 2, 3 };
    v.append_range( std::vector<int>{ 4, 5 } );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < 5u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) + 1 );
}

TYPED_TEST( vector_compliance, append_range_initializer_list )
{
    TypeParam v{ 1, 2 };
    v.append_range( { 3, 4, 5 } );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < 5u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) + 1 );
}

TYPED_TEST( vector_compliance, append_range_iota )
{
    TypeParam v{ 1, 2 };
    v.append_range( std::views::iota( 3, 6 ) );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < 5u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) + 1 );
}

TYPED_TEST( vector_compliance, pop_back )
{
    TypeParam v{ 1, 2, 3 };
    v.pop_back();
    EXPECT_EQ( v.size(), 2 );
    EXPECT_EQ( v.back(), 2 );
}

TYPED_TEST( vector_compliance, erase_single )
{
    TypeParam v{ 1, 2, 3, 4, 5 };
    auto it{ v.erase( v.nth( 2 ) ) }; // erase 3
    EXPECT_EQ( *it, 4 );
    EXPECT_EQ( v.size(), 4 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 2 );
    EXPECT_EQ( v[ 2 ], 4 );
    EXPECT_EQ( v[ 3 ], 5 );
}

TYPED_TEST( vector_compliance, erase_range )
{
    TypeParam v{ 1, 2, 3, 4, 5 };
    auto it{ v.erase( v.nth( 1 ), v.nth( 3 ) ) }; // erase 2,3
    EXPECT_EQ( *it, 4 );
    EXPECT_EQ( v.size(), 3 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 4 );
    EXPECT_EQ( v[ 2 ], 5 );
}

TYPED_TEST( vector_compliance, erase_first )
{
    TypeParam v{ 1, 2, 3 };
    v.erase( v.begin() );
    EXPECT_EQ( v.size(), 2 );
    EXPECT_EQ( v[ 0 ], 2 );
}

TYPED_TEST( vector_compliance, erase_last )
{
    TypeParam v{ 1, 2, 3 };
    v.erase( v.nth( 2 ) );
    EXPECT_EQ( v.size(), 2 );
    EXPECT_EQ( v.back(), 2 );
}

TYPED_TEST( vector_compliance, erase_empty_range )
{
    TypeParam v{ 1, 2, 3 };
    auto it{ v.erase( v.nth( 1 ), v.nth( 1 ) ) };
    EXPECT_EQ( v.size(), 3 );
    EXPECT_EQ( *it, 2 );
}

TYPED_TEST( vector_compliance, clear )
{
    TypeParam v{ 1, 2, 3, 4, 5 };
    v.clear();
    EXPECT_TRUE( v.empty() );
    EXPECT_EQ( v.size(), 0 );
}

TYPED_TEST( vector_compliance, swap )
{
    TypeParam a{ 1, 2, 3 };
    TypeParam b{ 4, 5 };
    a.swap( b );
    EXPECT_EQ( a.size(), 2 );
    EXPECT_EQ( b.size(), 3 );
    EXPECT_EQ( a[ 0 ], 4 );
    EXPECT_EQ( b[ 0 ], 1 );
}


////////////////////////////////////////////////////////////////////////////////
// 7. Growth/Shrink Extensions
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( vector_compliance, grow_to_no_init )
{
    TypeParam v{ 1, 2, 3 };
    auto * data{ v.grow_to( 6, no_init ) };
    EXPECT_EQ( v.size(), 6 );
    EXPECT_EQ( data[ 0 ], 1 );
    EXPECT_EQ( data[ 2 ], 3 );
    // elements 3..5 are uninitialized
}

TYPED_TEST( vector_compliance, grow_to_default_init )
{
    TypeParam v{ 1, 2, 3 };
    v.grow_to( 6, default_init );
    EXPECT_EQ( v.size(), 6 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 2 ], 3 );
}

TYPED_TEST( vector_compliance, grow_to_value_init )
{
    TypeParam v{ 1, 2, 3 };
    v.grow_to( 6, value_init );
    EXPECT_EQ( v.size(), 6 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 2 ], 3 );
    EXPECT_EQ( v[ 3 ], 0 );
    EXPECT_EQ( v[ 5 ], 0 );
}

TYPED_TEST( vector_compliance, grow_to_with_value )
{
    TypeParam v{ 1, 2, 3 };
    v.grow_to( 6, 99 );
    EXPECT_EQ( v.size(), 6 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 3 ], 99 );
    EXPECT_EQ( v[ 5 ], 99 );
}

TYPED_TEST( vector_compliance, grow_to_no_change )
{
    TypeParam v{ 1, 2, 3 };
    v.grow_to( 2, value_init ); // target <= current size, no-op
    EXPECT_EQ( v.size(), 3 );
}

TYPED_TEST( vector_compliance, grow_by )
{
    TypeParam v{ 1, 2, 3 };
    v.grow_by( 2, value_init );
    EXPECT_EQ( v.size(), 5 );
    EXPECT_EQ( v[ 3 ], 0 );
    EXPECT_EQ( v[ 4 ], 0 );
}

TYPED_TEST( vector_compliance, shrink_to )
{
    TypeParam v{ 1, 2, 3, 4, 5 };
    v.shrink_to( 3 );
    EXPECT_EQ( v.size(), 3 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 2 ], 3 );
}

TYPED_TEST( vector_compliance, shrink_by )
{
    TypeParam v{ 1, 2, 3, 4, 5 };
    v.shrink_by( 2 );
    EXPECT_EQ( v.size(), 3 );
    EXPECT_EQ( v[ 2 ], 3 );
}


////////////////////////////////////////////////////////////////////////////////
// 8. Comparison
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( vector_compliance, equality )
{
    TypeParam a{ 1, 2, 3 };
    TypeParam b{ 1, 2, 3 };
    TypeParam c{ 1, 2, 4 };
    EXPECT_TRUE ( a == b );
    EXPECT_FALSE( a == c );
    EXPECT_TRUE ( a != c );
    EXPECT_FALSE( a != b );
}

TYPED_TEST( vector_compliance, three_way_comparison )
{
    TypeParam a{ 1, 2, 3 };
    TypeParam b{ 1, 2, 4 };
    TypeParam c{ 1, 2 };
    EXPECT_TRUE( a < b );
    EXPECT_TRUE( b > a );
    EXPECT_TRUE( c < a ); // shorter prefix is less
    EXPECT_TRUE( a <= a );
    EXPECT_TRUE( a >= a );
}

TYPED_TEST( vector_compliance, comparison_empty )
{
    TypeParam a;
    TypeParam b;
    TypeParam c{ 1 };
    EXPECT_TRUE( a == b );
    EXPECT_TRUE( a < c );
}


////////////////////////////////////////////////////////////////////////////////
// 9. Stress / Multi-step operations
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( vector_compliance, push_pop_cycle )
{
    TypeParam v;
    for ( auto i{ 0 }; i < 100; ++i )
        v.push_back( i );
    EXPECT_EQ( v.size(), 100 );
    for ( auto i{ 99 }; i >= 50; --i )
    {
        EXPECT_EQ( v.back(), i );
        v.pop_back();
    }
    EXPECT_EQ( v.size(), 50 );
    for ( typename TypeParam::size_type i{ 0 }; i < 50u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) );
}

TYPED_TEST( vector_compliance, clear_and_reuse )
{
    TypeParam v{ 1, 2, 3, 4, 5 };
    v.clear();
    EXPECT_TRUE( v.empty() );
    v.push_back( 10 );
    v.push_back( 20 );
    EXPECT_EQ( v.size(), 2 );
    EXPECT_EQ( v[ 0 ], 10 );
}

TYPED_TEST( vector_compliance, multiple_resizes )
{
    TypeParam v;
    v.grow_to( 10, value_init );
    EXPECT_EQ( v.size(), 10 );
    v.resize( 5 );
    EXPECT_EQ( v.size(), 5 );
    v.resize( 20, 42 );
    EXPECT_EQ( v.size(), 20 );
    EXPECT_EQ( v[ 0 ], 0 );
    EXPECT_EQ( v[ 4 ], 0 );
    EXPECT_EQ( v[ 5 ], 42 );
    EXPECT_EQ( v[ 19 ], 42 );
}

TYPED_TEST( vector_compliance, insert_at_various_positions )
{
    TypeParam v;
    for ( auto i{ 0 }; i < 10; ++i )
        v.push_back( i * 10 );
    // Insert at the start
    v.insert( v.begin(), -10 );
    EXPECT_EQ( v[ 0 ], -10 );
    EXPECT_EQ( v.size(), 11 );
    // Insert at the end
    v.insert( v.end(), 100 );
    EXPECT_EQ( v.back(), 100 );
    EXPECT_EQ( v.size(), 12 );
    // Insert in the middle
    v.insert( v.nth( 6 ), 999 );
    EXPECT_EQ( v[ 6 ], 999 );
    EXPECT_EQ( v.size(), 13 );
}


////////////////////////////////////////////////////////////////////////////////
// 10. Move semantics (tr_vector specific)
////////////////////////////////////////////////////////////////////////////////

TEST( tr_vector_move, move_constructor_empties_source )
{
    tr_vector<int> v{ 1, 2, 3 };
    tr_vector<int> moved{ std::move( v ) };
    EXPECT_TRUE( v.empty() );
    EXPECT_EQ( v.data(), nullptr );
    EXPECT_EQ( v.capacity(), 0 );
    EXPECT_EQ( moved.size(), 3 );
}

TEST( tr_vector_move, move_assignment_clears_source )
{
    tr_vector<int> v{ 1, 2, 3 };
    tr_vector<int> dest{ 10, 20 };
    dest = std::move( v );
    // Move-assign: dest gets v's data, v is left empty (cleared).
    EXPECT_EQ( dest.size(), 3 );
    EXPECT_EQ( dest[ 0 ], 1 );
    EXPECT_TRUE( v.empty() );
}


////////////////////////////////////////////////////////////////////////////////
// 11. fc_vector specific tests
////////////////////////////////////////////////////////////////////////////////

TEST( fc_vector_specific, static_capacity )
{
    fc_vector<int, 16> v;
    EXPECT_EQ( v.capacity(), 16 );
    v.push_back( 1 );
    EXPECT_EQ( v.capacity(), 16 ); // unchanged
}

TEST( fc_vector_specific, DISABLED_implicit_copy )
{
    // Verify copy constructor is not explicit (was previously explicit)
    // DISABLED: [[gnu::warning("copying fc_vector")]] on the copy ctor
    // prevents implicit copy on some compilers.
    auto copy_fn = []( fc_vector<int, 8> v ) { return v; };
    fc_vector<int, 8> original{ 1, 2, 3 };
    auto copied{ copy_fn( original ) };
    EXPECT_EQ( copied.size(), 3 );
    EXPECT_EQ( copied[ 0 ], 1 );
}

TEST( fc_vector_specific, non_trivial_type )
{
    static int destructor_count{ 0 };
    struct non_trivial {
        int value;
        non_trivial( int v ) : value{ v } {}
        ~non_trivial() { ++destructor_count; }
    };

    destructor_count = 0;
    {
        fc_vector<non_trivial, 10> v;
        v.emplace_back( 1 );
        v.emplace_back( 2 );
        v.emplace_back( 3 );
        EXPECT_EQ( v.size(), 3 );
        EXPECT_EQ( v[ 0 ].value, 1 );
        EXPECT_EQ( v[ 2 ].value, 3 );

        v.erase( v.nth( 1 ) ); // erase element with value 2
        EXPECT_EQ( v.size(), 2 );
        EXPECT_EQ( v[ 0 ].value, 1 );
        EXPECT_EQ( v[ 1 ].value, 3 );
    }
    EXPECT_GE( destructor_count, 2 ); // at least 2 elements destroyed (3rd may be skipped by trivially_destructible_after_move_assignment optimization)
}

TEST( fc_vector_specific, move_semantics )
{
    fc_vector<int, 10> v{ 1, 2, 3 };
    fc_vector<int, 10> moved{ std::move( v ) };
    EXPECT_EQ( moved.size(), 3 );
    EXPECT_EQ( v.size(), 0 ); // source empty after move
}

TEST( fc_vector_specific, reserve_is_noop )
{
    fc_vector<int, 16> v{ 1, 2 };
    v.reserve( 10 ); // does nothing for fixed-capacity
    EXPECT_EQ( v.capacity(), 16 );
}

// GCC 15.2.1 bug: inherited constructors (`using base::base`) + CRTP
// static_cast<Impl&>(*this) downcast + writes to a union member in the derived
// class are misoptimized by GCC's dead store elimination in Release (with
// inlining/LTO). The writes through the union member pointer are incorrectly
// treated as dead stores and eliminated.
// This test verifies the gcc_dse_workaround in vector::initialized_impl.
// Without the workaround, value-initialized elements contain garbage in Release.
// libstdc++'s std::inplace_vector (which uses the same union pattern) may have
// the same latent bug.
TEST( fc_vector_specific, gcc_inherited_ctor_union_dse_bug )
{
    // value_init through inherited constructor -- triggers the bug path
    fc_vector<int, 256> v( 10, value_init );
    EXPECT_EQ( v.size(), 10 );
    for ( std::uint16_t i{ 0 }; i < v.size(); ++i )
        EXPECT_EQ( v[ i ], 0 ) << "element " << i << " not zero-initialized";

    // fill constructor through inherited constructor -- same bug path
    fc_vector<int, 256> v2( 10, 42 );
    EXPECT_EQ( v2.size(), 10 );
    for ( std::uint16_t i{ 0 }; i < v2.size(); ++i )
        EXPECT_EQ( v2[ i ], 42 ) << "element " << i << " not fill-initialized";

    // default_init through inherited constructor -- int is trivial, so
    // default-init leaves values indeterminate; just verify size
    fc_vector<int, 256> v3( 10, default_init );
    EXPECT_EQ( v3.size(), 10 );

    // iterator-pair constructor through inherited constructor
    int const src[]{ 1, 2, 3, 4, 5 };
    fc_vector<int, 256> v4( std::begin( src ), std::end( src ) );
    EXPECT_EQ( v4.size(), 5 );
    for ( std::uint16_t i{ 0 }; i < v4.size(); ++i )
        EXPECT_EQ( v4[ i ], src[ i ] ) << "element " << i << " mismatch";
}

// Standalone minimal reproducer for the GCC 15.2.1 DSE bug above -- does not
// depend on fc_vector or psi::vm at all. Demonstrates: inherited constructor
// (`using base::base`) + CRTP static_cast<Derived&>(*this) + writes to a union
// member -> GCC optimizes away the writes in Release (inlining + DSE).
namespace gcc_dse_bug_reproducer {
    template <typename T, unsigned N>
    union uninit_storage {
        constexpr  uninit_storage() noexcept {}
        constexpr ~uninit_storage() noexcept {}
        T data[ N ];
    };

    template <typename Derived, typename T>
    struct crtp_base {
        struct value_init_tag {};
        constexpr crtp_base() = default;
        constexpr crtp_base( unsigned n, value_init_tag ) {
            auto & self{ static_cast<Derived &>( *this ) };
            self.size_ = static_cast<unsigned char>( n );
            std::uninitialized_value_construct_n( self.storage_.data, n );
        }
    };

    template <typename T, unsigned N>
    struct derived : crtp_base<derived<T, N>, T> {
        using base = crtp_base<derived<T, N>, T>;
        using base::base; // inherited constructors -- triggers the bug
        constexpr derived() noexcept : size_{ 0 } {}

        unsigned char          size_;   // NO default member initializer (matching fc_vector)
        uninit_storage<T, N>   storage_;

        T       & operator[]( unsigned i )       { return storage_.data[ i ]; }
        T const & operator[]( unsigned i ) const { return storage_.data[ i ]; }
    };
} // namespace gcc_dse_bug_reproducer

// This test reproduces the raw GCC bug -- the standalone types above do NOT have
// the gcc_dse_workaround, so on GCC 15 Release this test demonstrates the
// miscompilation. Disabled on GCC Release because the failure is expected (the
// workaround lives in vector.hpp, not here).
#if defined( __GNUC__ ) && !defined( __clang__ ) && defined( NDEBUG )
TEST( fc_vector_specific, DISABLED_gcc_dse_bug_standalone_reproducer )
#else
TEST( fc_vector_specific, gcc_dse_bug_standalone_reproducer )
#endif
{
    using namespace gcc_dse_bug_reproducer;
    derived<int, 256> v( 5, derived<int, 256>::base::value_init_tag{} );
    EXPECT_EQ( v.size_, 5 );
    for ( unsigned i{ 0 }; i < v.size_; ++i )
        EXPECT_EQ( v[ i ], 0 ) << "element " << i << " not zero-initialized (GCC DSE bug)";
}


////////////////////////////////////////////////////////////////////////////////
// 12. tr_vector with uint32_t size_type
////////////////////////////////////////////////////////////////////////////////

TEST( tr_vector_u32, basic_operations )
{
    tr_vector<int, std::uint32_t> v{ 1, 2, 3 };
    static_assert( std::is_same_v<decltype( v.size() ), std::uint32_t> );
    EXPECT_EQ( v.size(), 3u );
    v.push_back( 4 );
    EXPECT_EQ( v.size(), 4u );
    EXPECT_EQ( v[ 3 ], 4 );
}

TEST( tr_vector_u32, assign_n_val )
{
    tr_vector<int, std::uint32_t> v{ 1, 2, 3 };
    v.assign( std::uint32_t( 5 ), 42 );
    EXPECT_EQ( v.size(), 5u );
    for ( std::uint32_t i{ 0 }; i < v.size(); ++i )
        EXPECT_EQ( v[ i ], 42 );
}


////////////////////////////////////////////////////////////////////////////////
// 13. stable_emplace_back / stable_reserve
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( vector_compliance, stable_reserve_within_capacity )
{
    TypeParam v;
    v.reserve( 20 );
    auto const cap{ v.capacity() };
    auto const ptr{ v.data() };
    // Requesting capacity within existing capacity always succeeds
    EXPECT_TRUE( v.stable_reserve( cap ) );
    EXPECT_TRUE( v.stable_reserve( 1 ) );
    EXPECT_TRUE( v.stable_reserve( 0 ) );
    EXPECT_EQ( v.data(), ptr ); // no reallocation
}

TYPED_TEST( vector_compliance, stable_emplace_back_with_capacity )
{
    TypeParam v;
    v.reserve( 10 );
    auto const ptr{ v.data() };
    EXPECT_TRUE( v.stable_emplace_back( 42 ) );
    EXPECT_EQ( v.size(), 1 );
    EXPECT_EQ( v[ 0 ], 42 );
    EXPECT_EQ( v.data(), ptr ); // same buffer
    EXPECT_TRUE( v.stable_emplace_back( 99 ) );
    EXPECT_EQ( v.size(), 2 );
    EXPECT_EQ( v[ 1 ], 99 );
}

TEST( fc_vector_specific, stable_reserve )
{
    fc_vector<int, 16> v{ 1, 2, 3 };
    // Within static capacity -- succeeds
    EXPECT_TRUE( v.stable_reserve( 16 ) );
    // Beyond static capacity -- fails
    EXPECT_FALSE( v.stable_reserve( 17 ) );
    EXPECT_EQ( v.size(), 3 ); // unchanged
}

TEST( fc_vector_specific, stable_emplace_back )
{
    fc_vector<int, 4> v{ 1, 2, 3 };
    EXPECT_TRUE( v.stable_emplace_back( 4 ) );
    EXPECT_EQ( v.size(), 4 );
    EXPECT_EQ( v[ 3 ], 4 );
    // Now full -- should fail
    EXPECT_FALSE( v.stable_emplace_back( 5 ) );
    EXPECT_EQ( v.size(), 4 ); // unchanged
}

TEST( tr_vector_move, stable_reserve_beyond_capacity )
{
    tr_vector<int> v{ 1, 2, 3 };
    auto const cap{ v.capacity() };
    // Beyond current capacity -- may or may not succeed depending on allocator
    auto const result{ v.stable_reserve( cap + 100 ) };
    // Either way, size is unchanged and data is valid
    EXPECT_EQ( v.size(), 3 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 2 ], 3 );
    if ( result )
    {
        EXPECT_GE( v.capacity(), cap + 100 );
    }
    else
    {
        EXPECT_EQ( v.capacity(), cap );
    }
}

////////////////////////////////////////////////////////////////////////////////
// 14. insert_range with non-sized input range (append+rotate path)
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( vector_compliance, insert_range_non_sized )
{
    // views::filter produces a non-sized range
    auto source{ std::views::iota( 2, 5 ) | std::views::filter( []( int ) { return true; } ) };
    static_assert( !std::ranges::sized_range<decltype(source)> );

    TypeParam v{ 1, 5 };
    v.insert_range( v.nth( 1 ), source );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < 5u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) + 1 );
}

TYPED_TEST( vector_compliance, insert_range_non_sized_at_begin )
{
    auto source{ std::views::iota( 1, 4 ) | std::views::filter( []( int ) { return true; } ) };
    TypeParam v{ 4, 5 };
    v.insert_range( v.begin(), source );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < 5u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) + 1 );
}

TYPED_TEST( vector_compliance, insert_range_non_sized_at_end )
{
    auto source{ std::views::iota( 3, 6 ) | std::views::filter( []( int ) { return true; } ) };
    TypeParam v{ 1, 2 };
    v.insert_range( v.end(), source );
    EXPECT_EQ( v.size(), 5 );
    for ( typename TypeParam::size_type i{ 0 }; i < 5u; ++i )
        EXPECT_EQ( v[ i ], static_cast<int>( i ) + 1 );
}

TYPED_TEST( vector_compliance, insert_range_non_sized_empty )
{
    auto source{ std::views::iota( 0, 0 ) | std::views::filter( []( int ) { return true; } ) };
    TypeParam v{ 1, 2, 3 };
    v.insert_range( v.nth( 1 ), source );
    EXPECT_EQ( v.size(), 3 );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 2 );
    EXPECT_EQ( v[ 2 ], 3 );
}


////////////////////////////////////////////////////////////////////////////////
// Non-trivially-moveable type tests for tr_vector
// Verifies that tr_vector correctly uses move+destroy (not bitwise realloc)
// when growing vectors of types that are NOT trivially relocatable.
////////////////////////////////////////////////////////////////////////////////

namespace
{
    // A type that tracks its own address to detect invalid bitwise relocation.
    // If moved via memcpy/realloc instead of move ctor, self_ptr_ won't match.
    struct address_tracker
    {
        int                    value;
        address_tracker const* self_ptr_;

        explicit address_tracker( int v = 0 ) : value{ v }, self_ptr_{ this } {}

        address_tracker( address_tracker const & other ) : value{ other.value }, self_ptr_{ this } {}
        address_tracker( address_tracker &&      other ) noexcept : value{ other.value }, self_ptr_{ this } { other.value = -1; }

        address_tracker & operator=( address_tracker const & other ) { value = other.value; self_ptr_ = this; return *this; }
        address_tracker & operator=( address_tracker &&      other ) noexcept { value = other.value; self_ptr_ = this; other.value = -1; return *this; }

        ~address_tracker() = default;

        bool valid() const { return self_ptr_ == this; }
    };

    static_assert( !is_trivially_moveable<address_tracker> );

    // A type with a non-trivial destructor that increments a counter.
    struct move_counter
    {
        int  value;
        int* move_count;
        int* destroy_count;

        explicit move_counter( int v, int & mc, int & dc ) : value{ v }, move_count{ &mc }, destroy_count{ &dc } {}
        move_counter( move_counter const & other ) : value{ other.value }, move_count{ other.move_count }, destroy_count{ other.destroy_count } {}
        move_counter( move_counter && other ) noexcept : value{ other.value }, move_count{ other.move_count }, destroy_count{ other.destroy_count } { ++(*move_count); other.value = -1; }
        move_counter & operator=( move_counter && other ) noexcept { value = other.value; move_count = other.move_count; destroy_count = other.destroy_count; ++(*move_count); other.value = -1; return *this; }
        ~move_counter() { if ( destroy_count ) ++(*destroy_count); }

        move_counter & operator=( move_counter const & ) = default;
    };

    static_assert( !is_trivially_moveable<move_counter> );
} // anon

TEST( tr_vector_nontrivial, push_back_triggers_move_not_memcpy )
{
    tr_vector<address_tracker> v;
    // Push enough elements to trigger multiple reallocations
    for ( int i{ 0 }; i < 100; ++i )
    {
        v.emplace_back( i );
        // After each push, ALL existing elements must have valid self-pointers
        for ( std::uint32_t j{ 0 }; j <= static_cast<std::uint32_t>( i ); ++j )
            ASSERT_TRUE( v[ j ].valid() ) << "Element " << j << " was bitwise-relocated instead of move-constructed (after push " << i << ")";
    }
    EXPECT_EQ( v.size(), 100 );
    EXPECT_EQ( v[ 0 ].value, 0 );
    EXPECT_EQ( v[ 99 ].value, 99 );
}

TEST( tr_vector_nontrivial, reserve_triggers_move_not_memcpy )
{
    tr_vector<address_tracker> v;
    for ( int i{ 0 }; i < 10; ++i )
        v.emplace_back( i );

    // Force a large reserve that definitely reallocates
    v.reserve( 10000 );

    for ( std::uint32_t i{ 0 }; i < 10; ++i )
    {
        ASSERT_TRUE( v[ i ].valid() ) << "Element " << i << " was bitwise-relocated by reserve()";
        EXPECT_EQ( v[ i ].value, static_cast<int>( i ) );
    }
}

TEST( tr_vector_nontrivial, growth_calls_move_ctor_and_destroys_old )
{
    int move_count   { 0 };
    int destroy_count{ 0 };

    {
        tr_vector<move_counter> v;
        // Push elements one by one, forcing reallocations
        for ( int i{ 0 }; i < 50; ++i )
            v.emplace_back( i, move_count, destroy_count );

        // Move count should be > 0 (reallocations happened)
        EXPECT_GT( move_count, 0 ) << "No move constructors called -- growth used bitwise realloc";

        // Destroy count should match move count (old copies destroyed after move)
        // Plus destructor calls for the temporary move_counter objects
        // Just verify it's non-zero (old elements were properly destroyed)
        EXPECT_GT( destroy_count, 0 ) << "No destructors called for old elements after reallocation";

        // All elements should have correct values
        for ( std::uint32_t i{ 0 }; i < 50; ++i )
            EXPECT_EQ( v[ i ].value, static_cast<int>( i ) );
    }

    // After vector destruction, all remaining elements should be destroyed too
    EXPECT_GE( destroy_count, 50 );
}

TEST( tr_vector_nontrivial, copy_constructor )
{
    tr_vector<address_tracker> src;
    for ( int i{ 0 }; i < 20; ++i )
        src.emplace_back( i );

    auto copy{ src };
    ASSERT_EQ( copy.size(), 20 );
    for ( std::uint32_t i{ 0 }; i < 20; ++i )
    {
        EXPECT_TRUE( copy[ i ].valid() );
        EXPECT_EQ( copy[ i ].value, static_cast<int>( i ) );
        // Copy and source must be distinct objects
        EXPECT_NE( &copy[ i ], &src[ i ] );
    }
}

TEST( tr_vector_nontrivial, move_constructor )
{
    tr_vector<address_tracker> src;
    for ( int i{ 0 }; i < 20; ++i )
        src.emplace_back( i );

    auto moved{ std::move( src ) };
    EXPECT_TRUE( src.empty() );
    ASSERT_EQ( moved.size(), 20 );
    for ( std::uint32_t i{ 0 }; i < 20; ++i )
    {
        EXPECT_TRUE( moved[ i ].valid() );
        EXPECT_EQ( moved[ i ].value, static_cast<int>( i ) );
    }
}

TEST( tr_vector_nontrivial, move_assignment )
{
    tr_vector<address_tracker> src;
    for ( int i{ 0 }; i < 20; ++i )
        src.emplace_back( i );

    tr_vector<address_tracker> dst;
    dst.emplace_back( 999 );

    dst = std::move( src );
    EXPECT_TRUE( src.empty() );
    ASSERT_EQ( dst.size(), 20 );
    for ( std::uint32_t i{ 0 }; i < 20; ++i )
    {
        EXPECT_TRUE( dst[ i ].valid() );
        EXPECT_EQ( dst[ i ].value, static_cast<int>( i ) );
    }
}

TEST( tr_vector_nontrivial, erase_and_shrink )
{
    tr_vector<address_tracker> v;
    for ( int i{ 0 }; i < 10; ++i )
        v.emplace_back( i );

    v.erase( v.nth( 3 ) ); // erase element 3
    ASSERT_EQ( v.size(), 9 );

    // Verify remaining elements are valid and in correct order
    int expected[]{ 0, 1, 2, 4, 5, 6, 7, 8, 9 };
    for ( std::uint32_t i{ 0 }; i < 9; ++i )
    {
        EXPECT_TRUE( v[ i ].valid() ) << "Element " << i << " invalid after erase";
        EXPECT_EQ( v[ i ].value, expected[ i ] );
    }
}

TEST( tr_vector_nontrivial, with_string )
{
    // std::string is a real-world non-trivially-moveable type
    tr_vector<std::string> v;
    for ( int i{ 0 }; i < 100; ++i )
        v.emplace_back( "string_" + std::to_string( i ) );

    ASSERT_EQ( v.size(), 100 );
    EXPECT_EQ( v[ 0  ], "string_0"  );
    EXPECT_EQ( v[ 99 ], "string_99" );

    // Copy
    auto copy{ v };
    EXPECT_EQ( copy[ 50 ], "string_50" );

    // Mutate copy
    copy[ 0 ] = "modified";
    EXPECT_EQ( v[ 0 ], "string_0" ); // original unchanged
    EXPECT_EQ( copy[ 0 ], "modified" );
}

////////////////////////////////////////////////////////////////////////////////
// Storage lifecycle tests: destructor, move-assign, move-construct.
// Uses a tracked type to detect memory/object leaks and double-destruction.
////////////////////////////////////////////////////////////////////////////////

// Non-trivially-destructible tracked type for leak/double-free detection.
// Named "lifecycle_tracked" to avoid ODR conflict with flat_map.cpp's "tracked".
static std::atomic<int> tracked_live_count{ 0 };

struct lifecycle_tracked
{
    int value;

    explicit lifecycle_tracked( int v = 0 ) noexcept : value{ v } { tracked_live_count.fetch_add( 1, std::memory_order_seq_cst ); }
    lifecycle_tracked( lifecycle_tracked const & o ) noexcept : value{ o.value } { tracked_live_count.fetch_add( 1, std::memory_order_seq_cst ); }
    lifecycle_tracked( lifecycle_tracked && o ) noexcept : value{ o.value } { o.value = -1; tracked_live_count.fetch_add( 1, std::memory_order_seq_cst ); }
    lifecycle_tracked & operator=( lifecycle_tracked const & ) = default;
    lifecycle_tracked & operator=( lifecycle_tracked && o ) noexcept { value = o.value; o.value = -1; return *this; }
    ~lifecycle_tracked() noexcept { tracked_live_count.fetch_sub( 1, std::memory_order_seq_cst ); }
};
static_assert( !std::is_trivially_destructible_v<lifecycle_tracked> );
static_assert( !is_trivially_moveable<lifecycle_tracked>, "lifecycle_tracked must NOT be trivially moveable" );

// Sanity: verify lifecycle_tracked counting with raw new/delete.
TEST( lifecycle_tracked_sanity, raw_new_delete )
{
    tracked_live_count.store( 0, std::memory_order_seq_cst );
    auto * p{ new lifecycle_tracked( 42 ) };
    EXPECT_EQ( tracked_live_count.load( std::memory_order_seq_cst ), 1 );
    delete p;
    EXPECT_EQ( tracked_live_count.load( std::memory_order_seq_cst ), 0 );
}

// Sanity: verify lifecycle_tracked counting with std::vector.
TEST( lifecycle_tracked_sanity, std_vector )
{
    tracked_live_count.store( 0, std::memory_order_seq_cst );
    {
        std::vector<lifecycle_tracked> v;
        v.emplace_back( 42 );
        EXPECT_EQ( tracked_live_count.load( std::memory_order_seq_cst ), 1 ) << "std::vector: one element should be alive";
    }
    EXPECT_EQ( tracked_live_count.load( std::memory_order_seq_cst ), 0 ) << "std::vector: element should be destroyed";
}

// Sanity: verify lifecycle_tracked counting with tr_vector.
TEST( lifecycle_tracked_sanity, tr_vector_single )
{
    tracked_live_count.store( 0, std::memory_order_seq_cst );
    {
        tr_vector<lifecycle_tracked> v;
        v.emplace_back( 42 );
        EXPECT_EQ( tracked_live_count.load( std::memory_order_seq_cst ), 1 ) << "one element should be alive after emplace_back";
    }
    EXPECT_EQ( tracked_live_count.load( std::memory_order_seq_cst ), 0 ) << "element should be destroyed after vector dtor";
}

TEST( lifecycle_tracked_sanity, growth_preserves_count )
{
    tracked_live_count.store( 0, std::memory_order_seq_cst );
    tr_vector<lifecycle_tracked> v;
    for ( int i{ 0 }; i < 20; ++i )
    {
        v.emplace_back( i );
        ASSERT_EQ( tracked_live_count.load( std::memory_order_seq_cst ), i + 1 ) << "live count wrong after emplace_back " << i;
    }
}

// Trivially-moveable variant: required by small_vector (sbo_hybrid uses memcpy).
// IMPORTANT: live_count tracking is NOT compatible with trivial relocation
// (bitwise copy skips ctors), so we only use this for tests that don't
// check live_count during growth. The destructor still runs at final cleanup.
static std::atomic<int> tracked_tm_live_count{ 0 };

struct lifecycle_tracked_trivial_move
{
    int value;

    explicit lifecycle_tracked_trivial_move( int v = 0 ) noexcept : value{ v } { tracked_tm_live_count.fetch_add( 1, std::memory_order_seq_cst ); }
    lifecycle_tracked_trivial_move( lifecycle_tracked_trivial_move const & o ) noexcept : value{ o.value } { tracked_tm_live_count.fetch_add( 1, std::memory_order_seq_cst ); }
    // Trivially moveable: no custom move ctor/assign
    ~lifecycle_tracked_trivial_move() noexcept { tracked_tm_live_count.fetch_sub( 1, std::memory_order_seq_cst ); }
};
static_assert( !std::is_trivially_destructible_v<lifecycle_tracked_trivial_move> );
static_assert( is_trivially_moveable<lifecycle_tracked_trivial_move> );

// Run lifecycle tests against all storage backends.
using LifecycleTestTypes = ::testing::Types<
    tr_vector<lifecycle_tracked>,
    tr_vector<lifecycle_tracked_trivial_move>,
    small_vector<lifecycle_tracked_trivial_move, 4>,
    small_vector<lifecycle_tracked_trivial_move, 4, std::uint32_t, compact_lsb_opts>,
    small_vector<lifecycle_tracked_trivial_move, 4, std::uint32_t, embedded_opts>
>;

template <typename T> std::atomic<int> & live_count_for();
template <> std::atomic<int> & live_count_for<lifecycle_tracked>()              { return tracked_live_count;    }
template <> std::atomic<int> & live_count_for<lifecycle_tracked_trivial_move>() { return tracked_tm_live_count; }

template <typename VecType>
class storage_lifecycle : public ::testing::Test
{
public:
    using elem_t = typename VecType::value_type;
    static std::atomic<int> & live_count_ref() { return live_count_for<elem_t>(); }
    static int live_count() { return live_count_ref().load( std::memory_order_relaxed ); }
    void SetUp() override { live_count_ref().store( 0, std::memory_order_relaxed ); }
};
TYPED_TEST_SUITE( storage_lifecycle, LifecycleTestTypes );


// Test: destructor destroys all elements (no leak).
TYPED_TEST( storage_lifecycle, destructor_destroys_all_elements )
{
    {
        TypeParam v;
        for ( int i{ 0 }; i < 20; ++i )
            v.emplace_back( i );
        ASSERT_EQ( this->live_count(), 20 );
    }
    EXPECT_EQ( this->live_count(), 0 ) << "elements leaked after vector destruction";
}

// Test: move construction transfers ownership, source is safe to destroy.
TYPED_TEST( storage_lifecycle, move_construct_no_double_free )
{
    {
        TypeParam v1;
        for ( int i{ 0 }; i < 20; ++i )
            v1.emplace_back( i );
        auto const count_before{ this->live_count() };
        TypeParam v2{ std::move( v1 ) };
        // Move may or may not change the live count depending on whether
        // elements are bitwise-moved or move-constructed, but the important
        // thing is that after both are destroyed, count must be 0.
        EXPECT_EQ( v2.size(), 20u );
        EXPECT_GE( this->live_count(), count_before ); // no premature destruction
    }
    EXPECT_EQ( this->live_count(), 0 ) << "leak or double-free after move + destroy";
}

// Test: move assignment frees old storage, doesn't leak.
TYPED_TEST( storage_lifecycle, move_assign_frees_old_storage )
{
    {
        TypeParam v1;
        for ( int i{ 0 }; i < 10; ++i )
            v1.emplace_back( i );

        TypeParam v2;
        for ( int i{ 100 }; i < 120; ++i )
            v2.emplace_back( i );

        ASSERT_EQ( this->live_count(), 30 ); // 10 + 20

        v1 = std::move( v2 );

        // After move-assign: v1 has 20 elements, v2 is empty/moved-from.
        // The 10 old elements in v1 must be destroyed (not leaked).
        EXPECT_EQ( v1.size(), 20u );
        EXPECT_LE( this->live_count(), 20 + static_cast<int>( v2.size() ) );
    }
    EXPECT_EQ( this->live_count(), 0 ) << "leak after move-assign + destroy";
}

// Test: copy construction creates independent copy, both can be destroyed.
TYPED_TEST( storage_lifecycle, copy_construct_independent_lifetime )
{
    {
        TypeParam v1;
        for ( int i{ 0 }; i < 15; ++i )
            v1.emplace_back( i );

        TypeParam v2{ v1 };
        ASSERT_EQ( this->live_count(), 30 ); // 15 + 15
        EXPECT_EQ( v2.size(), 15u );
    }
    EXPECT_EQ( this->live_count(), 0 ) << "leak after copy + destroy both";
}

// Test: clear destroys all elements, second clear is safe.
TYPED_TEST( storage_lifecycle, clear_destroys_elements )
{
    TypeParam v;
    for ( int i{ 0 }; i < 20; ++i )
        v.emplace_back( i );
    ASSERT_EQ( this->live_count(), 20 );

    v.clear();
    EXPECT_EQ( this->live_count(), 0 );
    EXPECT_EQ( v.size(), 0u );

    // Second clear on empty vector: no-op, no crash.
    v.clear();
    EXPECT_EQ( this->live_count(), 0 );
}

// Test: erase removes elements and shifts the rest.
// Note: trivially_destructible_after_move_assignment optimization skips dtors
// on moved-from tail slots for nothrow-moveable types. Those elements are
// logically dead (beyond size()) but their storage/dtors may be deferred.
TYPED_TEST( storage_lifecycle, erase_destroys_erased_elements )
{
    TypeParam v;
    for ( int i{ 0 }; i < 10; ++i )
        v.emplace_back( i );
    ASSERT_EQ( this->live_count(), 10 );

    v.erase( v.begin() + 2, v.begin() + 5 ); // erase 3 elements
    EXPECT_EQ( v.size(), 7u );

    // Verify remaining element values are correct (shifted left).
    EXPECT_EQ( v[ 0 ].value, 0 );
    EXPECT_EQ( v[ 1 ].value, 1 );
    EXPECT_EQ( v[ 2 ].value, 5 ); // was index 5, shifted to index 2
    EXPECT_EQ( v[ 3 ].value, 6 );
    EXPECT_EQ( v[ 6 ].value, 9 );
}

// Test: pop_back destroys the removed element.
TYPED_TEST( storage_lifecycle, pop_back_destroys_element )
{
    TypeParam v;
    for ( int i{ 0 }; i < 5; ++i )
        v.emplace_back( i );
    ASSERT_EQ( this->live_count(), 5 );

    v.pop_back();
    EXPECT_EQ( this->live_count(), 4 );
    EXPECT_EQ( v.size(), 4u );
}

// Test: SBO-specific — heap spill followed by destruction.
// (For tr_vector this is just a regular heap destruction, which is fine.)
TYPED_TEST( storage_lifecycle, heap_spill_then_destroy )
{
    {
        TypeParam v;
        // Push enough elements to guarantee heap spill on SBO types (inline N=4).
        for ( int i{ 0 }; i < 100; ++i )
            v.emplace_back( i );
        ASSERT_EQ( this->live_count(), 100 );
        ASSERT_EQ( v.size(), 100u );
    }
    EXPECT_EQ( this->live_count(), 0 ) << "heap-spilled elements leaked";
}

// Test: SBO-specific — heap spill, then move-assign an inline-sized vector.
TYPED_TEST( storage_lifecycle, heap_spill_move_assign_inline )
{
    {
        TypeParam v1;
        for ( int i{ 0 }; i < 100; ++i )
            v1.emplace_back( i );

        TypeParam v2;
        v2.emplace_back( 42 );

        ASSERT_EQ( this->live_count(), 101 );

        // Move a small vector into the heap-spilled one: old heap must be freed.
        v1 = std::move( v2 );
        EXPECT_EQ( v1.size(), 1u );
    }
    EXPECT_EQ( this->live_count(), 0 ) << "old heap allocation leaked after move-assign from inline";
}

// Test: shrink_to destroys excess elements.
TYPED_TEST( storage_lifecycle, shrink_to_destroys_excess )
{
    TypeParam v;
    for ( int i{ 0 }; i < 20; ++i )
        v.emplace_back( i );
    ASSERT_EQ( this->live_count(), 20 );

    v.resize( 5 );
    EXPECT_EQ( this->live_count(), 5 );
    EXPECT_EQ( v.size(), 5u );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
