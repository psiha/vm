// Comprehensive std::vector-like compliance tests for tr_vector and fc_vector
// using Google Test typed tests to cover both implementations uniformly.
#include <psi/vm/containers/fc_vector.hpp>
#include <psi/vm/containers/tr_vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <ranges>
#include <span>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// Typed test suite — runs every test against all vector types
////////////////////////////////////////////////////////////////////////////////

using VectorTestTypes = ::testing::Types<
    tr_vector<int>,
    tr_vector<int, std::uint32_t>,
    fc_vector<int, 256>
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
// This test verifies the gcc_dse_workaround in vector_impl::initialized_impl.
// Without the workaround, value-initialized elements contain garbage in Release.
// libstdc++'s std::inplace_vector (which uses the same union pattern) may have
// the same latent bug.
TEST( fc_vector_specific, gcc_inherited_ctor_union_dse_bug )
{
    // value_init through inherited constructor — triggers the bug path
    fc_vector<int, 256> v( 10, value_init );
    EXPECT_EQ( v.size(), 10 );
    for ( std::uint16_t i{ 0 }; i < v.size(); ++i )
        EXPECT_EQ( v[ i ], 0 ) << "element " << i << " not zero-initialized";

    // fill constructor through inherited constructor — same bug path
    fc_vector<int, 256> v2( 10, 42 );
    EXPECT_EQ( v2.size(), 10 );
    for ( std::uint16_t i{ 0 }; i < v2.size(); ++i )
        EXPECT_EQ( v2[ i ], 42 ) << "element " << i << " not fill-initialized";

    // default_init through inherited constructor — int is trivial, so
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

// Standalone minimal reproducer for the GCC 15.2.1 DSE bug above — does not
// depend on fc_vector or psi::vm at all. Demonstrates: inherited constructor
// (`using base::base`) + CRTP static_cast<Derived&>(*this) + writes to a union
// member → GCC optimizes away the writes in Release (inlining + DSE).
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
        using base::base; // inherited constructors — triggers the bug
        constexpr derived() noexcept : size_{ 0 } {}

        unsigned char          size_;   // NO default member initializer (matching fc_vector)
        uninit_storage<T, N>   storage_;

        T       & operator[]( unsigned i )       { return storage_.data[ i ]; }
        T const & operator[]( unsigned i ) const { return storage_.data[ i ]; }
    };
} // namespace gcc_dse_bug_reproducer

// This test reproduces the raw GCC bug — the standalone types above do NOT have
// the gcc_dse_workaround, so on GCC 15 Release this test demonstrates the
// miscompilation. Disabled on GCC Release because the failure is expected (the
// workaround lives in vector_impl.hpp, not here).
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
    // Within static capacity — succeeds
    EXPECT_TRUE( v.stable_reserve( 16 ) );
    // Beyond static capacity — fails
    EXPECT_FALSE( v.stable_reserve( 17 ) );
    EXPECT_EQ( v.size(), 3 ); // unchanged
}

TEST( fc_vector_specific, stable_emplace_back )
{
    fc_vector<int, 4> v{ 1, 2, 3 };
    EXPECT_TRUE( v.stable_emplace_back( 4 ) );
    EXPECT_EQ( v.size(), 4 );
    EXPECT_EQ( v[ 3 ], 4 );
    // Now full — should fail
    EXPECT_FALSE( v.stable_emplace_back( 5 ) );
    EXPECT_EQ( v.size(), 4 ); // unchanged
}

TEST( tr_vector_move, stable_reserve_beyond_capacity )
{
    tr_vector<int> v{ 1, 2, 3 };
    auto const cap{ v.capacity() };
    // Beyond current capacity — may or may not succeed depending on allocator
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


//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
