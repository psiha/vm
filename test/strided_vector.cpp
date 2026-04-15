// Compliance tests for psi::vm::strided_vector.
//
// Covers the std::vector-compatible subset (size/capacity/element access/
// iteration/insert/erase/resize/swap/comparisons) adapted to strided
// "entry = span<T, dynamic_extent>" semantics, plus the strided-specific
// extensions (ensureCapacity, push_back_fill, extractData/adoptData).
//
// Parameterized on T, stride integer type and storage backend so every
// supported configuration is exercised uniformly.

#include <psi/vm/containers/fc_vector.hpp>
#include <psi/vm/containers/heap_vector.hpp>
#include <psi/vm/containers/strided_vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// Typed test suite — runs every test against all strided_vector configurations
////////////////////////////////////////////////////////////////////////////////

using StridedVectorTestTypes = ::testing::Types<
    strided_vector<std::uint32_t>,                                                                         // default stride=uint8_t, heap_storage
    strided_vector<std::uint32_t, std::uint16_t>,                                                          // wider stride int
    strided_vector<std::uint32_t, std::uint32_t>,                                                          // stride==size_type width
    strided_vector<std::uint32_t, std::uint8_t,  heap_storage<std::uint32_t, std::uint32_t>>,              // 32-bit size backing
    strided_vector<std::int64_t,  std::uint8_t>,                                                           // 64-bit element
    strided_vector<char,          std::uint8_t>                                                            // single-byte element
>;

template <typename SV>
class strided_vector_compliance : public ::testing::Test {};
TYPED_TEST_SUITE( strided_vector_compliance, StridedVectorTestTypes );

namespace
{
    // Build a std::vector<T> from int-valued literals. Templated on the
    // strided_vector TypeParam itself so call sites read `make_entry<TypeParam>`
    // and the element type is derived automatically.
    template <typename SV>
    [[ nodiscard ]] auto make_entry( std::initializer_list<int> const vals )
    {
        using T = typename SV::value_type;
        std::vector<T> v;
        v.reserve( vals.size() );
        for ( auto const x : vals ) v.push_back( static_cast<T>( x ) );
        return v;
    }

    template <typename T>
    [[ nodiscard ]] std::span<T const> as_span( std::vector<T> const & v ) noexcept
    {
        return { v.data(), v.size() };
    }
}

////////////////////////////////////////////////////////////////////////////////
// 1. Construction
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( strided_vector_compliance, default_constructor )
{
    TypeParam v;
    EXPECT_TRUE( v.empty() );
    EXPECT_EQ  ( v.size (), 0u );
    EXPECT_EQ  ( v.stride(), 0u );
}

TYPED_TEST( strided_vector_compliance, stride_constructor )
{
    TypeParam v( 4 );
    EXPECT_TRUE( v.empty() );
    EXPECT_EQ  ( v.size (), 0u );
    EXPECT_EQ  ( v.stride(), 4u );
}

TYPED_TEST( strided_vector_compliance, stride_count_constructor_default_inits )
{
    TypeParam v( 3, 5 );
    EXPECT_EQ( v.stride(), 3u );
    EXPECT_EQ( v.size  (), 5u );
    EXPECT_FALSE( v.empty() );
    for ( auto const & entry : v )
        EXPECT_EQ( entry.size(), 3u );
}

TYPED_TEST( strided_vector_compliance, stride_count_prototype_constructor )
{
    auto const prototype{ make_entry<TypeParam>( { 7, 8, 9, 10 } ) };
    TypeParam v( 4, 3, as_span( prototype ) );
    EXPECT_EQ( v.stride(), 4u );
    EXPECT_EQ( v.size  (), 3u );
    for ( auto const & entry : v )
        EXPECT_TRUE( std::ranges::equal( entry, prototype ) );
}

TYPED_TEST( strided_vector_compliance, entry_range_constructor )
{
    auto const e0{ make_entry<TypeParam>( { 1, 2 } ) };
    auto const e1{ make_entry<TypeParam>( { 3, 4 } ) };
    auto const e2{ make_entry<TypeParam>( { 5, 6 } ) };

    using T = typename TypeParam::value_type;
    std::vector<std::span<T const>> entries{
        std::span<T const>{ e0.data(), e0.size() },
        std::span<T const>{ e1.data(), e1.size() },
        std::span<T const>{ e2.data(), e2.size() }
    };

    TypeParam v( 2, entries );
    EXPECT_EQ( v.stride(), 2u );
    EXPECT_EQ( v.size  (), 3u );
    EXPECT_TRUE( std::ranges::equal( v[ 0 ], e0 ) );
    EXPECT_TRUE( std::ranges::equal( v[ 1 ], e1 ) );
    EXPECT_TRUE( std::ranges::equal( v[ 2 ], e2 ) );
}

TYPED_TEST( strided_vector_compliance, copy_constructor )
{
    TypeParam a( 3 );
    a.push_back( as_span( make_entry<TypeParam>( { 1, 2, 3 } ) ) );
    a.push_back( as_span( make_entry<TypeParam>( { 4, 5, 6 } ) ) );
    TypeParam b{ a };
    EXPECT_EQ( a, b );
    EXPECT_EQ( b.stride(), a.stride() );
    EXPECT_EQ( b.size  (), a.size  () );
}

TYPED_TEST( strided_vector_compliance, move_constructor_transfers_state )
{
    TypeParam a( 3 );
    a.push_back( as_span( make_entry<TypeParam>( { 1, 2, 3 } ) ) );
    auto const old_size{ a.size() };
    TypeParam b{ std::move( a ) };
    EXPECT_EQ( b.size  (), old_size );
    EXPECT_EQ( b.stride(), 3u       );
}

TYPED_TEST( strided_vector_compliance, copy_assignment )
{
    TypeParam a( 2 );
    a.push_back( as_span( make_entry<TypeParam>( { 10, 20 } ) ) );
    TypeParam b;
    b = a;
    EXPECT_EQ( a, b );
}

TYPED_TEST( strided_vector_compliance, move_assignment )
{
    TypeParam a( 2 );
    a.push_back( as_span( make_entry<TypeParam>( { 10, 20 } ) ) );
    TypeParam b;
    b = std::move( a );
    EXPECT_EQ( b.size  (), 1u );
    EXPECT_EQ( b.stride(), 2u );
}

////////////////////////////////////////////////////////////////////////////////
// 2. Capacity
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( strided_vector_compliance, init_sets_stride_and_resets )
{
    TypeParam v;
    v.init( 3 );
    EXPECT_EQ  ( v.stride(), 3u );
    EXPECT_TRUE( v.empty ()     );
    v.push_back( as_span( make_entry<TypeParam>( { 1, 2, 3 } ) ) );
    EXPECT_EQ( v.size(), 1u );
    v.init( 2 );
    EXPECT_EQ  ( v.stride(), 2u );
    EXPECT_TRUE( v.empty ()     );
}

TYPED_TEST( strided_vector_compliance, reserve_grows_capacity )
{
    TypeParam v( 3 );
    v.reserve( 1000 );
    EXPECT_GE( v.capacity(), 1000u );
    EXPECT_EQ( v.size    (),    0u );
    auto const cap_before{ v.capacity() };
    for ( int i{ 0 }; i < 1000; ++i )
        v.push_back( as_span( make_entry<TypeParam>( { i, i + 1, i + 2 } ) ) );
    EXPECT_GE( v.capacity(), cap_before );
    EXPECT_EQ( v.size    (), 1000u      );
}

TYPED_TEST( strided_vector_compliance, capacity_scales_with_stride )
{
    TypeParam v( 4 );
    v.reserve( 10 );
    // capacity() in entries is at least what we reserved
    EXPECT_GE( v.capacity(), 10u );
}

TYPED_TEST( strided_vector_compliance, capacity_bytes_matches_backing )
{
    TypeParam v( 3 );
    v.reserve( 16 );
    using T = typename TypeParam::value_type;
    EXPECT_GE( v.capacityBytes(), 16u * 3u * sizeof( T ) );
}

TYPED_TEST( strided_vector_compliance, max_entries_positive )
{
    TypeParam v( 4 );
    EXPECT_GT( v.maxEntries(), 0u );
}

TYPED_TEST( strided_vector_compliance, shrink_to_fit_reduces_capacity )
{
    TypeParam v( 2 );
    v.reserve( 500 );
    EXPECT_GE( v.capacity(), 500u );
    v.push_back( as_span( make_entry<TypeParam>( { 1, 2 } ) ) );
    v.shrink_to_fit();
    EXPECT_GE( v.capacity(), v.size() );
}

////////////////////////////////////////////////////////////////////////////////
// 3. Element access
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( strided_vector_compliance, operator_index_returns_span_of_stride )
{
    TypeParam v( 3 );
    auto const e0{ make_entry<TypeParam>( { 1, 2, 3 } ) };
    auto const e1{ make_entry<TypeParam>( { 4, 5, 6 } ) };
    v.push_back( as_span( e0 ) );
    v.push_back( as_span( e1 ) );
    EXPECT_EQ  ( v[ 0 ].size(), 3u );
    EXPECT_TRUE( std::ranges::equal( v[ 0 ], e0 ) );
    EXPECT_TRUE( std::ranges::equal( v[ 1 ], e1 ) );
}

TYPED_TEST( strided_vector_compliance, at_throws_on_out_of_range )
{
    TypeParam v( 2 );
    v.push_back( as_span( make_entry<TypeParam>( { 1, 2 } ) ) );
    EXPECT_NO_THROW( (void)v.at( 0 ) );
    EXPECT_THROW   ( (void)v.at( 1 ), std::out_of_range );
    EXPECT_THROW   ( (void)v.at( 100 ), std::out_of_range );
}

TYPED_TEST( strided_vector_compliance, front_back_access )
{
    TypeParam v( 3 );
    auto const e0{ make_entry<TypeParam>( { 10, 20, 30 } ) };
    auto const e1{ make_entry<TypeParam>( { 40, 50, 60 } ) };
    v.push_back( as_span( e0 ) );
    v.push_back( as_span( e1 ) );
    EXPECT_TRUE( std::ranges::equal( v.front(), e0 ) );
    EXPECT_TRUE( std::ranges::equal( v.back (), e1 ) );
}

TYPED_TEST( strided_vector_compliance, data_returns_flat_buffer )
{
    TypeParam v( 2 );
    v.push_back( as_span( make_entry<TypeParam>( { 1, 2 } ) ) );
    v.push_back( as_span( make_entry<TypeParam>( { 3, 4 } ) ) );
    auto * const p{ v.data() };
    EXPECT_EQ( p[ 0 ], static_cast<typename TypeParam::value_type>( 1 ) );
    EXPECT_EQ( p[ 3 ], static_cast<typename TypeParam::value_type>( 4 ) );
}

////////////////////////////////////////////////////////////////////////////////
// 4. Iterators (random-access with span proxy reference)
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( strided_vector_compliance, iterator_is_random_access )
{
    using It = typename TypeParam::iterator;
    static_assert( std::random_access_iterator<It> );
    static_assert( std::is_same_v<typename std::iterator_traits<It>::iterator_category,
                                  std::random_access_iterator_tag> );
}

TYPED_TEST( strided_vector_compliance, const_iterator_is_random_access )
{
    using It = typename TypeParam::const_iterator;
    static_assert( std::random_access_iterator<It> );
}

TYPED_TEST( strided_vector_compliance, iterator_arithmetic )
{
    TypeParam v( 2 );
    for ( int i{ 0 }; i < 5; ++i )
        v.push_back( as_span( make_entry<TypeParam>( { i, i * 2 } ) ) );

    auto const it0{ v.begin()     };
    auto const it2{ v.begin() + 2 };
    EXPECT_EQ( it2 - it0, 2 );
    EXPECT_EQ( v.end() - v.begin(), 5 );
    EXPECT_TRUE( std::ranges::equal( *it2, make_entry<TypeParam>( { 2, 4 } ) ) );
}

TYPED_TEST( strided_vector_compliance, iterator_traverses_every_entry )
{
    TypeParam v( 3 );
    for ( int i{ 0 }; i < 4; ++i )
        v.push_back( as_span( make_entry<TypeParam>( { i, i, i } ) ) );

    std::size_t idx{ 0 };
    for ( auto const entry : v )
    {
        auto const expected{ make_entry<TypeParam>( { static_cast<int>( idx ), static_cast<int>( idx ), static_cast<int>( idx ) } ) };
        EXPECT_TRUE( std::ranges::equal( entry, expected ) );
        ++idx;
    }
    EXPECT_EQ( idx, 4u );
}

TYPED_TEST( strided_vector_compliance, reverse_iterator_traverses_backwards )
{
    TypeParam v( 2 );
    for ( int i{ 0 }; i < 3; ++i )
        v.push_back( as_span( make_entry<TypeParam>( { i, i } ) ) );

    std::size_t idx{ 3 };
    for ( auto rit{ v.rbegin() }; rit != v.rend(); ++rit )
    {
        --idx;
        auto const expected{ make_entry<TypeParam>( { static_cast<int>( idx ), static_cast<int>( idx ) } ) };
        EXPECT_TRUE( std::ranges::equal( *rit, expected ) );
    }
}

TYPED_TEST( strided_vector_compliance, const_iterator_from_non_const )
{
    TypeParam v( 2 );
    v.push_back( as_span( make_entry<TypeParam>( { 7, 8 } ) ) );
    typename TypeParam::const_iterator ci{ v.begin() };
    EXPECT_TRUE( std::ranges::equal( *ci, make_entry<TypeParam>( { 7, 8 } ) ) );
}

////////////////////////////////////////////////////////////////////////////////
// 5. Modifiers
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( strided_vector_compliance, push_back_grows )
{
    TypeParam v( 3 );
    for ( int i{ 0 }; i < 100; ++i )
        v.push_back( as_span( make_entry<TypeParam>( { i, i * 2, i * 3 } ) ) );
    EXPECT_EQ( v.size(), 100u );
    for ( std::size_t i{ 0 }; i < v.size(); ++i )
    {
        auto const expected{ make_entry<TypeParam>(
            { static_cast<int>( i ), static_cast<int>( i * 2 ), static_cast<int>( i * 3 ) } ) };
        EXPECT_TRUE( std::ranges::equal( v[ i ], expected ) );
    }
}

TYPED_TEST( strided_vector_compliance, push_back_fill )
{
    TypeParam v( 4 );
    v.push_back_fill( static_cast<typename TypeParam::value_type>( 42 ) );
    EXPECT_EQ( v.size(), 1u );
    for ( auto const x : v[ 0 ] )
        EXPECT_EQ( x, static_cast<typename TypeParam::value_type>( 42 ) );
}

TYPED_TEST( strided_vector_compliance, emplace_back_from_scalars )
{
    TypeParam v( 3 );
    auto ref{ v.emplace_back( 1, 2, 3 ) };
    EXPECT_EQ( v.size(), 1u );
    EXPECT_TRUE( std::ranges::equal( ref, make_entry<TypeParam>( { 1, 2, 3 } ) ) );
}

TYPED_TEST( strided_vector_compliance, pop_back )
{
    TypeParam v( 2 );
    v.push_back( as_span( make_entry<TypeParam>( { 1, 2 } ) ) );
    v.push_back( as_span( make_entry<TypeParam>( { 3, 4 } ) ) );
    v.pop_back();
    EXPECT_EQ  ( v.size(), 1u );
    EXPECT_TRUE( std::ranges::equal( v[ 0 ], make_entry<TypeParam>( { 1, 2 } ) ) );
}

TYPED_TEST( strided_vector_compliance, clear_preserves_stride )
{
    TypeParam v( 3 );
    v.push_back( as_span( make_entry<TypeParam>( { 1, 2, 3 } ) ) );
    v.clear();
    EXPECT_TRUE( v.empty() );
    EXPECT_EQ  ( v.stride(), 3u );
    v.push_back( as_span( make_entry<TypeParam>( { 7, 8, 9 } ) ) );
    EXPECT_EQ( v.size(), 1u );
}

TYPED_TEST( strided_vector_compliance, insert_middle )
{
    TypeParam v( 2 );
    auto const e0{ make_entry<TypeParam>( { 1, 2 } ) };
    auto const e1{ make_entry<TypeParam>( { 5, 6 } ) };
    auto const em{ make_entry<TypeParam>( { 3, 4 } ) };
    v.push_back( as_span( e0 ) );
    v.push_back( as_span( e1 ) );
    auto it{ v.insert( v.begin() + 1, as_span( em ) ) };
    EXPECT_EQ  ( it - v.begin(), 1 );
    EXPECT_EQ  ( v.size(), 3u );
    EXPECT_TRUE( std::ranges::equal( v[ 0 ], e0 ) );
    EXPECT_TRUE( std::ranges::equal( v[ 1 ], em ) );
    EXPECT_TRUE( std::ranges::equal( v[ 2 ], e1 ) );
}

TYPED_TEST( strided_vector_compliance, insert_at_end )
{
    TypeParam v( 2 );
    v.push_back( as_span( make_entry<TypeParam>( { 1, 2 } ) ) );
    auto const en{ make_entry<TypeParam>( { 9, 9 } ) };
    v.insert( v.end(), as_span( en ) );
    EXPECT_EQ  ( v.size(), 2u );
    EXPECT_TRUE( std::ranges::equal( v.back(), en ) );
}

TYPED_TEST( strided_vector_compliance, insert_count_copies )
{
    TypeParam v( 2 );
    v.push_back( as_span( make_entry<TypeParam>( { 1, 2 } ) ) );
    auto const proto{ make_entry<TypeParam>( { 7, 7 } ) };
    v.insert( v.begin(), 3, as_span( proto ) );
    EXPECT_EQ( v.size(), 4u );
    for ( std::size_t i{ 0 }; i < 3; ++i )
        EXPECT_TRUE( std::ranges::equal( v[ i ], proto ) );
    EXPECT_TRUE( std::ranges::equal( v[ 3 ], make_entry<TypeParam>( { 1, 2 } ) ) );
}

TYPED_TEST( strided_vector_compliance, erase_single )
{
    TypeParam v( 2 );
    for ( int i{ 0 }; i < 4; ++i )
        v.push_back( as_span( make_entry<TypeParam>( { i, i } ) ) );
    auto it{ v.erase( v.begin() + 1 ) };
    EXPECT_EQ  ( it - v.begin(), 1 );
    EXPECT_EQ  ( v.size(), 3u );
    EXPECT_TRUE( std::ranges::equal( v[ 0 ], make_entry<TypeParam>( { 0, 0 } ) ) );
    EXPECT_TRUE( std::ranges::equal( v[ 1 ], make_entry<TypeParam>( { 2, 2 } ) ) );
    EXPECT_TRUE( std::ranges::equal( v[ 2 ], make_entry<TypeParam>( { 3, 3 } ) ) );
}

TYPED_TEST( strided_vector_compliance, erase_range )
{
    TypeParam v( 2 );
    for ( int i{ 0 }; i < 5; ++i )
        v.push_back( as_span( make_entry<TypeParam>( { i, i } ) ) );
    v.erase( v.begin() + 1, v.begin() + 4 );
    EXPECT_EQ  ( v.size(), 2u );
    EXPECT_TRUE( std::ranges::equal( v[ 0 ], make_entry<TypeParam>( { 0, 0 } ) ) );
    EXPECT_TRUE( std::ranges::equal( v[ 1 ], make_entry<TypeParam>( { 4, 4 } ) ) );
}

TYPED_TEST( strided_vector_compliance, resize_grows )
{
    TypeParam v( 3 );
    v.push_back( as_span( make_entry<TypeParam>( { 1, 2, 3 } ) ) );
    v.resize( 4 );
    EXPECT_EQ( v.size(), 4u );
    EXPECT_TRUE( std::ranges::equal( v[ 0 ], make_entry<TypeParam>( { 1, 2, 3 } ) ) );
}

TYPED_TEST( strided_vector_compliance, resize_shrinks )
{
    TypeParam v( 2 );
    for ( int i{ 0 }; i < 5; ++i )
        v.push_back( as_span( make_entry<TypeParam>( { i, i } ) ) );
    v.resize( 2 );
    EXPECT_EQ( v.size(), 2u );
    EXPECT_TRUE( std::ranges::equal( v[ 0 ], make_entry<TypeParam>( { 0, 0 } ) ) );
    EXPECT_TRUE( std::ranges::equal( v[ 1 ], make_entry<TypeParam>( { 1, 1 } ) ) );
}

TYPED_TEST( strided_vector_compliance, resize_with_prototype )
{
    TypeParam v( 3 );
    auto const proto{ make_entry<TypeParam>( { 9, 9, 9 } ) };
    v.resize( 3, as_span( proto ) );
    EXPECT_EQ( v.size(), 3u );
    for ( auto const entry : v )
        EXPECT_TRUE( std::ranges::equal( entry, proto ) );
}

TYPED_TEST( strided_vector_compliance, swap_member )
{
    TypeParam a( 2 );
    a.push_back( as_span( make_entry<TypeParam>( { 1, 2 } ) ) );
    TypeParam b( 2 );
    b.push_back( as_span( make_entry<TypeParam>( { 9, 9 } ) ) );
    b.push_back( as_span( make_entry<TypeParam>( { 8, 8 } ) ) );
    a.swap( b );
    EXPECT_EQ( a.size(), 2u );
    EXPECT_EQ( b.size(), 1u );
}

TYPED_TEST( strided_vector_compliance, swap_free )
{
    TypeParam a( 2 );
    a.push_back( as_span( make_entry<TypeParam>( { 1, 2 } ) ) );
    TypeParam b( 2 );
    b.push_back( as_span( make_entry<TypeParam>( { 3, 4 } ) ) );
    b.push_back( as_span( make_entry<TypeParam>( { 5, 6 } ) ) );
    swap( a, b );
    EXPECT_EQ( a.size(), 2u );
    EXPECT_EQ( b.size(), 1u );
}

////////////////////////////////////////////////////////////////////////////////
// 6. Non-standard extensions
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( strided_vector_compliance, ensure_capacity_amortizes )
{
    TypeParam v( 4 );
    v.ensureCapacity( 1 );
    EXPECT_GE( v.capacity(), 1u );
    v.ensureCapacity( 1000 );
    EXPECT_GE( v.capacity(), 1000u );
}

TYPED_TEST( strided_vector_compliance, extract_adopt_round_trip )
{
    TypeParam v( 3 );
    for ( int i{ 0 }; i < 5; ++i )
        v.push_back( as_span( make_entry<TypeParam>( { i, i, i } ) ) );
    auto const cap_before{ v.capacityBytes() };
    auto buf{ v.extractData() };
    EXPECT_GE( static_cast<std::size_t>( buf.capacity() ) * sizeof( typename TypeParam::value_type ), cap_before );
    EXPECT_TRUE( v.empty() );
    v.adoptData( std::move( buf ) );
    EXPECT_TRUE( v.empty() );                    // adopt clears contents
    EXPECT_GE  ( v.capacityBytes(), cap_before ); // but keeps capacity
}

////////////////////////////////////////////////////////////////////////////////
// 7. Geometric growth stress
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( strided_vector_compliance, geometric_growth_stress )
{
    TypeParam v( 4 );
    constexpr std::uint32_t N{ 10'000 };
    for ( std::uint32_t i{ 0 }; i < N; ++i )
    {
        auto const entry{ make_entry<TypeParam>(
            { static_cast<int>( i ), static_cast<int>( i * 2 ), static_cast<int>( i * 3 ), static_cast<int>( i * 4 ) } ) };
        v.push_back( as_span( entry ) );
    }
    EXPECT_EQ( v.size(), N );
    for ( std::uint32_t i{ 0 }; i < N; ++i )
    {
        auto const expected{ make_entry<TypeParam>(
            { static_cast<int>( i ), static_cast<int>( i * 2 ), static_cast<int>( i * 3 ), static_cast<int>( i * 4 ) } ) };
        EXPECT_TRUE( std::ranges::equal( v[ i ], expected ) );
    }
}

////////////////////////////////////////////////////////////////////////////////
// 8. Comparison operators
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( strided_vector_compliance, equality )
{
    TypeParam a( 2 ), b( 2 );
    a.push_back( as_span( make_entry<TypeParam>( { 1, 2 } ) ) );
    b.push_back( as_span( make_entry<TypeParam>( { 1, 2 } ) ) );
    EXPECT_EQ( a, b );
    b.push_back( as_span( make_entry<TypeParam>( { 3, 4 } ) ) );
    EXPECT_NE( a, b );
}

TYPED_TEST( strided_vector_compliance, ordering_lex )
{
    TypeParam a( 2 ), b( 2 );
    a.push_back( as_span( make_entry<TypeParam>( { 1, 2 } ) ) );
    b.push_back( as_span( make_entry<TypeParam>( { 1, 3 } ) ) );
    EXPECT_LT( a, b );
    EXPECT_GT( b, a );
}

////////////////////////////////////////////////////////////////////////////////
// 9. erase_if free function
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( strided_vector_compliance, erase_if_by_predicate )
{
    TypeParam v( 2 );
    for ( int i{ 0 }; i < 6; ++i )
        v.push_back( as_span( make_entry<TypeParam>( { i, i } ) ) );

    // Drop even-first-coord entries
    auto const removed{ erase_if( v, []( auto const entry ) {
        return entry[ 0 ] % 2 == 0;
    } ) };
    EXPECT_EQ( removed, 3u );
    EXPECT_EQ( v.size(), 3u );
    for ( auto const entry : v )
        EXPECT_NE( entry[ 0 ] % 2, 0 );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
