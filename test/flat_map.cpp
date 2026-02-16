////////////////////////////////////////////////////////////////////////////////
/// psi::vm::flat_map unit tests
////////////////////////////////////////////////////////////////////////////////

#include <psi/vm/containers/flat_map.hpp>
#include <psi/vm/containers/tr_vector.hpp>
#include <psi/vm/containers/is_trivially_moveable.hpp>

#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <string_view>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm {
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// Typed test infrastructure
////////////////////////////////////////////////////////////////////////////////

template <typename KC, typename MC>
struct map_config
{
    using key_container    = KC;
    using mapped_container = MC;
    using key_type         = typename KC::value_type;
    using mapped_type      = typename MC::value_type;
    using map_type         = flat_map<key_type, mapped_type, std::less<key_type>, KC, MC>;
};

template <typename C>
class flat_map_typed : public ::testing::Test {};

using map_configs = ::testing::Types<
    map_config< std::vector<int>,              std::vector<std::string_view> >,
    map_config< tr_vector<int>,                tr_vector<std::string_view>   >,
    map_config< std::deque<int>,               std::deque<std::string_view>  >,
    map_config< tr_vector<int, std::uint32_t>, std::deque<std::string_view>  >,
    map_config< std::deque<int>,               std::vector<std::string_view> >
>;

TYPED_TEST_SUITE( flat_map_typed, map_configs );

////////////////////////////////////////////////////////////////////////////////
// Construction
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( flat_map_typed, default_construction )
{
    using map_t = typename TypeParam::map_type;
    map_t m;
    EXPECT_TRUE( m.empty() );
    EXPECT_EQ( m.size(), 0u );
    EXPECT_EQ( m.begin(), m.end() );
}

TYPED_TEST( flat_map_typed, initializer_list_construction )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 3, "c" }, { 1, "a" }, { 2, "b" } };
    EXPECT_EQ( m.size(), 3u );
    EXPECT_EQ( m.at( 1 ), "a" );
    EXPECT_EQ( m.at( 2 ), "b" );
    EXPECT_EQ( m.at( 3 ), "c" );
    // Verify sorted
    auto const & keys{ m.keys() };
    EXPECT_TRUE( std::is_sorted( keys.begin(), keys.end() ) );
}

TYPED_TEST( flat_map_typed, range_construction )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    std::vector<std::pair<KT, MT>> pairs{
        { 5, "e" }, { 1, "a" }, { 3, "c" }, { 1, "dup" }
    };
    map_t m( pairs.begin(), pairs.end() );
    EXPECT_EQ( m.size(), 3u );
    EXPECT_EQ( m.at( 1 ), "a" ); // first wins
    EXPECT_EQ( m.at( 3 ), "c" );
    EXPECT_EQ( m.at( 5 ), "e" );
}

TYPED_TEST( flat_map_typed, copy_construction )
{
    using map_t = typename TypeParam::map_type;
    map_t src{ { 1, "a" }, { 2, "b" } };
    map_t dst{ src };
    EXPECT_EQ( dst.size(), 2u );
    EXPECT_EQ( dst.at( 1 ), "a" );
    EXPECT_EQ( dst.at( 2 ), "b" );
    // Verify independence
    src[ 1 ] = "Z";
    EXPECT_EQ( dst.at( 1 ), "a" );
}

TYPED_TEST( flat_map_typed, move_construction )
{
    using map_t = typename TypeParam::map_type;
    map_t src{ { 1, "a" }, { 2, "b" } };
    map_t dst{ std::move( src ) };
    EXPECT_EQ( dst.size(), 2u );
    EXPECT_EQ( dst.at( 1 ), "a" );
}

TYPED_TEST( flat_map_typed, sorted_unique_container_construction )
{
    using map_t = typename TypeParam::map_type;
    using KC    = typename TypeParam::key_container;
    using MC    = typename TypeParam::mapped_container;
    using MT    = typename TypeParam::mapped_type;
    KC keys;
    keys.push_back( 1 ); keys.push_back( 3 ); keys.push_back( 5 );
    MC vals;
    vals.push_back( MT{ "a" } ); vals.push_back( MT{ "c" } ); vals.push_back( MT{ "e" } );
    map_t m( sorted_unique, std::move( keys ), std::move( vals ) );
    EXPECT_EQ( m.size(), 3u );
    EXPECT_EQ( m.at( 1 ), "a" );
    EXPECT_EQ( m.at( 3 ), "c" );
    EXPECT_EQ( m.at( 5 ), "e" );
}

TYPED_TEST( flat_map_typed, unsorted_container_construction )
{
    using map_t = typename TypeParam::map_type;
    using KC    = typename TypeParam::key_container;
    using MC    = typename TypeParam::mapped_container;
    using MT    = typename TypeParam::mapped_type;
    KC keys;
    keys.push_back( 3 ); keys.push_back( 1 ); keys.push_back( 2 );
    MC vals;
    vals.push_back( MT{ "c" } ); vals.push_back( MT{ "a" } ); vals.push_back( MT{ "b" } );
    map_t m( std::move( keys ), std::move( vals ) );
    EXPECT_EQ( m.size(), 3u );
    EXPECT_EQ( m.at( 1 ), "a" );
    EXPECT_EQ( m.at( 2 ), "b" );
    EXPECT_EQ( m.at( 3 ), "c" );
}

TYPED_TEST( flat_map_typed, unsorted_container_with_duplicates )
{
    using map_t = typename TypeParam::map_type;
    using KC    = typename TypeParam::key_container;
    using MC    = typename TypeParam::mapped_container;
    using MT    = typename TypeParam::mapped_type;
    KC keys;
    keys.push_back( 3 ); keys.push_back( 1 ); keys.push_back( 3 ); keys.push_back( 2 );
    MC vals;
    vals.push_back( MT{ "c" } ); vals.push_back( MT{ "a" } ); vals.push_back( MT{ "X" } ); vals.push_back( MT{ "b" } );
    map_t m( std::move( keys ), std::move( vals ) );
    EXPECT_EQ( m.size(), 3u );
    EXPECT_EQ( m.at( 3 ), "c" ); // first wins after sort
}

TYPED_TEST( flat_map_typed, from_range_construction )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    std::vector<std::pair<KT, MT>> src{ { 3, "c" }, { 1, "a" }, { 2, "b" } };
    map_t m( std::from_range, src );
    EXPECT_EQ( m.size(), 3u );
    EXPECT_TRUE( std::is_sorted( m.keys().begin(), m.keys().end() ) );
    EXPECT_EQ( m.at( 1 ), "a" );
    EXPECT_EQ( m.at( 2 ), "b" );
    EXPECT_EQ( m.at( 3 ), "c" );
}

TYPED_TEST( flat_map_typed, from_range_with_duplicates )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    std::vector<std::pair<KT, MT>> src{ { 1, "a" }, { 1, "X" }, { 2, "b" } };
    map_t m( std::from_range, src );
    EXPECT_EQ( m.size(), 2u );
    EXPECT_EQ( m.at( 1 ), "a" ); // first occurrence wins
}

////////////////////////////////////////////////////////////////////////////////
// Element access
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( flat_map_typed, operator_bracket )
{
    using map_t = typename TypeParam::map_type;
    map_t m;
    m[ 3 ] = "c";
    m[ 1 ] = "a";
    m[ 2 ] = "b";
    EXPECT_EQ( m.size(), 3u );
    EXPECT_EQ( m[ 1 ], "a" );
    EXPECT_EQ( m[ 2 ], "b" );
    EXPECT_EQ( m[ 3 ], "c" );
    // Overwrite existing — size unchanged
    m[ 1 ] = "A";
    EXPECT_EQ( m.size(), 3u );
    EXPECT_EQ( m[ 1 ], "A" );
}

TYPED_TEST( flat_map_typed, at_throws_on_missing )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" } };
    EXPECT_EQ( m.at( 1 ), "a" );
    EXPECT_THROW( m.at( 2 ), std::out_of_range );
}

TYPED_TEST( flat_map_typed, const_at )
{
    using map_t = typename TypeParam::map_type;
    map_t const m{ { 1, "a" }, { 2, "b" } };
    EXPECT_EQ( m.at( 1 ), "a" );
    EXPECT_THROW( m.at( 99 ), std::out_of_range );
}

TYPED_TEST( flat_map_typed, try_emplace )
{
    using map_t = typename TypeParam::map_type;
    map_t m;
    auto [it1, ok1]{ m.try_emplace( 2, "two" ) };
    EXPECT_TRUE ( ok1 );
    EXPECT_EQ   ( it1->first, 2 );
    EXPECT_EQ   ( it1->second, "two" );

    auto [it2, ok2]{ m.try_emplace( 2, "TWO" ) };
    EXPECT_FALSE( ok2 );
    EXPECT_EQ   ( it2->second, "two" ); // not overwritten
}

////////////////////////////////////////////////////////////////////////////////
// Insertion
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( flat_map_typed, insert_single )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    map_t m;
    auto [it, ok]{ m.insert( std::pair<KT, MT>{ 5, "e" } ) };
    EXPECT_TRUE( ok );
    EXPECT_EQ  ( it->first, 5 );
    EXPECT_EQ  ( it->second, "e" );

    auto [it2, ok2]{ m.insert( std::pair<KT, MT>{ 5, "X" } ) };
    EXPECT_FALSE( ok2 );
    EXPECT_EQ   ( it2->second, "e" );
}

TYPED_TEST( flat_map_typed, insert_or_assign )
{
    using map_t = typename TypeParam::map_type;
    map_t m;
    auto [it1, ok1]{ m.insert_or_assign( 1, std::string_view{ "a" } ) };
    EXPECT_TRUE( ok1 );
    EXPECT_EQ  ( it1->second, "a" );

    auto [it2, ok2]{ m.insert_or_assign( 1, std::string_view{ "A" } ) };
    EXPECT_FALSE( ok2 );
    EXPECT_EQ   ( it2->second, "A" ); // overwritten
}

TYPED_TEST( flat_map_typed, emplace )
{
    using map_t = typename TypeParam::map_type;
    map_t m;
    auto [it, ok]{ m.emplace( 3, "c" ) };
    EXPECT_TRUE( ok );
    EXPECT_EQ  ( it->first, 3 );
}

TYPED_TEST( flat_map_typed, emplace_hint )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 5, "e" }, { 9, "i" } };
    // Wrong hint: insert 3 with hint at begin (should be between 1 and 5)
    auto it{ m.emplace_hint( m.begin(), 3, "c" ) };
    EXPECT_EQ( it->first, 3 );
    EXPECT_EQ( it->second, "c" );
    EXPECT_EQ( m.size(), 4u );
    // Existing key hint
    auto it2{ m.emplace_hint( m.begin() + 1, 5, "X" ) };
    EXPECT_EQ( it2->second, "e" ); // not overwritten
    EXPECT_EQ( m.size(), 4u );
    // Verify sorted
    auto const & keys{ m.keys() };
    EXPECT_TRUE( std::is_sorted( keys.begin(), keys.end() ) );
}

TYPED_TEST( flat_map_typed, insert_hint )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    map_t m{ { 1, "a" }, { 5, "e" } };
    auto it{ m.insert( m.begin() + 1, std::pair<KT, MT>{ 3, "c" } ) };
    EXPECT_EQ( it->first, 3 );
    EXPECT_EQ( it->second, "c" );
    EXPECT_EQ( m.size(), 3u );
}

TYPED_TEST( flat_map_typed, insert_or_assign_hinted )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 3, "c" }, { 5, "e" } };
    // Hit: update existing
    auto it{ m.insert_or_assign( m.begin() + 1, 3, std::string_view{ "C" } ) };
    EXPECT_EQ( it->second, "C" );
    EXPECT_EQ( m.size(), 3u );
    // Miss: insert new
    auto it2{ m.insert_or_assign( m.begin(), 4, std::string_view{ "d" } ) };
    EXPECT_EQ( it2->second, "d" );
    EXPECT_EQ( m.size(), 4u );
}

////////////////////////////////////////////////////////////////////////////////
// Lookup
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( flat_map_typed, find_hit_and_miss )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 3, "c" }, { 5, "e" } };
    auto it{ m.find( 3 ) };
    ASSERT_NE( it, m.end() );
    EXPECT_EQ( it->second, "c" );

    EXPECT_EQ( m.find( 2 ), m.end() );
    EXPECT_EQ( m.find( 0 ), m.end() );
    EXPECT_EQ( m.find( 99 ), m.end() );
}

TYPED_TEST( flat_map_typed, lower_upper_bound )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 3, "c" }, { 5, "e" } };
    auto lb{ m.lower_bound( 3 ) };
    EXPECT_EQ( lb->first, 3 );
    auto ub{ m.upper_bound( 3 ) };
    EXPECT_EQ( ub->first, 5 );

    auto lb2{ m.lower_bound( 2 ) };
    EXPECT_EQ( lb2->first, 3 ); // first element >= 2
}

TYPED_TEST( flat_map_typed, equal_range )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 3, "c" }, { 5, "e" } };
    auto [lo, hi]{ m.equal_range( 3 ) };
    EXPECT_EQ( lo->first, 3 );
    EXPECT_EQ( hi->first, 5 );
    EXPECT_EQ( hi - lo, 1 );
}

TYPED_TEST( flat_map_typed, contains_and_count )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 3, "c" } };
    EXPECT_TRUE ( m.contains( 1 ) );
    EXPECT_FALSE( m.contains( 2 ) );
    EXPECT_EQ   ( m.count( 1 ), 1u );
    EXPECT_EQ   ( m.count( 2 ), 0u );
}

////////////////////////////////////////////////////////////////////////////////
// Erasure
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( flat_map_typed, erase_by_key )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 2, "b" }, { 3, "c" } };
    EXPECT_EQ( m.erase( 2 ), 1u );
    EXPECT_EQ( m.size(), 2u );
    EXPECT_FALSE( m.contains( 2 ) );

    EXPECT_EQ( m.erase( 99 ), 0u );
    EXPECT_EQ( m.size(), 2u );
}

TYPED_TEST( flat_map_typed, erase_by_iterator )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 2, "b" }, { 3, "c" } };
    auto it{ m.find( 2 ) };
    auto next{ m.erase( it ) };
    EXPECT_EQ( m.size(), 2u );
    EXPECT_EQ( next->first, 3 );
}

TYPED_TEST( flat_map_typed, erase_range )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 2, "b" }, { 3, "c" }, { 4, "d" } };
    auto first{ m.find( 2 ) };
    auto last { m.find( 4 ) };
    m.erase( first, last );
    EXPECT_EQ( m.size(), 2u );
    EXPECT_TRUE ( m.contains( 1 ) );
    EXPECT_FALSE( m.contains( 2 ) );
    EXPECT_FALSE( m.contains( 3 ) );
    EXPECT_TRUE ( m.contains( 4 ) );
}

////////////////////////////////////////////////////////////////////////////////
// Extract/Replace
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( flat_map_typed, extract_and_replace )
{
    using map_t = typename TypeParam::map_type;
    using MT    = typename TypeParam::mapped_type;
    map_t m{ { 1, "a" }, { 2, "b" }, { 3, "c" } };
    auto c{ m.extract() };
    EXPECT_TRUE( m.empty() );
    EXPECT_EQ( c.keys.size(), 3u );
    EXPECT_EQ( c.values.size(), 3u );

    // Modify extracted containers
    c.keys.push_back( 4 );
    c.values.push_back( MT{ "d" } );

    m.replace( std::move( c.keys ), std::move( c.values ) );
    EXPECT_EQ( m.size(), 4u );
    EXPECT_EQ( m.at( 4 ), "d" );
}

////////////////////////////////////////////////////////////////////////////////
// Merge
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( flat_map_typed, merge_non_overlapping )
{
    using map_t = typename TypeParam::map_type;
    map_t a{ { 1, "a" }, { 3, "c" } };
    map_t b{ { 2, "b" }, { 4, "d" } };
    a.merge( b );
    EXPECT_EQ( a.size(), 4u );
    EXPECT_TRUE( b.empty() );
    EXPECT_EQ( a.at( 2 ), "b" );
    EXPECT_EQ( a.at( 4 ), "d" );
}

TYPED_TEST( flat_map_typed, merge_overlapping )
{
    using map_t = typename TypeParam::map_type;
    map_t a{ { 1, "a" }, { 3, "c" } };
    map_t b{ { 2, "b" }, { 3, "C" } };
    a.merge( b );
    EXPECT_EQ( a.size(), 3u );
    EXPECT_EQ( a.at( 3 ), "c" ); // original kept
    EXPECT_EQ( b.size(), 1u );   // conflicting element remains
    EXPECT_EQ( b.at( 3 ), "C" );
}

TYPED_TEST( flat_map_typed, merge_rvalue )
{
    using map_t = typename TypeParam::map_type;
    map_t target{ { 1, "a" }, { 3, "c" } };
    map_t source{ { 2, "b" }, { 3, "C" }, { 4, "d" } };
    target.merge( std::move( source ) );
    EXPECT_EQ( target.size(), 4u );
    EXPECT_EQ( target.at( 3 ), "c" ); // existing wins
    EXPECT_EQ( target.at( 2 ), "b" );
    EXPECT_EQ( target.at( 4 ), "d" );
    // source is cleared after rvalue merge
    EXPECT_TRUE( source.empty() );
}

TYPED_TEST( flat_map_typed, merge_empty_source )
{
    using map_t = typename TypeParam::map_type;
    map_t target{ { 1, "a" } };
    map_t source;
    target.merge( source );
    EXPECT_EQ( target.size(), 1u );
}

TYPED_TEST( flat_map_typed, merge_empty_target )
{
    using map_t = typename TypeParam::map_type;
    map_t target;
    map_t source{ { 1, "a" }, { 2, "b" } };
    target.merge( source );
    EXPECT_EQ( target.size(), 2u );
    EXPECT_TRUE( source.empty() );
}

TYPED_TEST( flat_map_typed, merge_self )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 2, "b" } };
    m.merge( m );
    EXPECT_EQ( m.size(), 2u ); // no change
}

////////////////////////////////////////////////////////////////////////////////
// Bulk/Range operations
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( flat_map_typed, insert_range )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    map_t m{ { 1, "a" }, { 3, "c" }, { 5, "e" } };
    std::vector<std::pair<KT, MT>> src{ { 2, "b" }, { 4, "d" }, { 6, "f" } };
    m.insert_range( src );
    EXPECT_EQ( m.size(), 6u );
    for ( int i{ 1 }; i <= 6; ++i )
    {
        std::string_view expected{ &"abcdef"[ i - 1 ], 1 };
        EXPECT_EQ( m.at( i ), expected );
    }
}

TYPED_TEST( flat_map_typed, insert_range_with_duplicates )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    map_t m{ { 1, "a" }, { 3, "c" }, { 5, "e" } };
    std::vector<std::pair<KT, MT>> src{ { 3, "X" }, { 5, "Y" }, { 7, "g" } };
    m.insert_range( src );
    EXPECT_EQ( m.size(), 4u );
    // Existing values win
    EXPECT_EQ( m.at( 3 ), "c" );
    EXPECT_EQ( m.at( 5 ), "e" );
    EXPECT_EQ( m.at( 7 ), "g" );
}

TYPED_TEST( flat_map_typed, insert_range_into_empty )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    map_t m;
    std::vector<std::pair<KT, MT>> src{ { 5, "e" }, { 1, "a" }, { 3, "c" } };
    m.insert_range( src );
    EXPECT_EQ( m.size(), 3u );
    EXPECT_TRUE( std::is_sorted( m.keys().begin(), m.keys().end() ) );
    EXPECT_EQ( m.at( 1 ), "a" );
    EXPECT_EQ( m.at( 3 ), "c" );
    EXPECT_EQ( m.at( 5 ), "e" );
}

TYPED_TEST( flat_map_typed, insert_range_sorted_unique )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    map_t m{ { 2, "b" }, { 4, "d" } };
    std::vector<std::pair<KT, MT>> src{ { 1, "a" }, { 3, "c" }, { 5, "e" } };
    m.insert_range( psi::vm::sorted_unique, src );
    EXPECT_EQ( m.size(), 5u );
    EXPECT_EQ( m.at( 1 ), "a" );
    EXPECT_EQ( m.at( 3 ), "c" );
    EXPECT_EQ( m.at( 5 ), "e" );
}

TYPED_TEST( flat_map_typed, insert_range_empty )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    map_t m{ { 1, "a" } };
    std::vector<std::pair<KT, MT>> empty;
    m.insert_range( empty );
    EXPECT_EQ( m.size(), 1u );
}

TYPED_TEST( flat_map_typed, bulk_insert_iterator )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    map_t m{ { 1, "a" }, { 5, "e" } };
    std::vector<std::pair<KT, MT>> src{ { 3, "c" }, { 2, "b" }, { 4, "d" }, { 1, "X" } };
    m.insert( src.begin(), src.end() );
    EXPECT_EQ( m.size(), 5u );
    EXPECT_EQ( m.at( 1 ), "a" ); // existing wins
    EXPECT_EQ( m.at( 2 ), "b" );
    EXPECT_EQ( m.at( 3 ), "c" );
}

TYPED_TEST( flat_map_typed, insert_sorted_unique_iterator )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    map_t m{ { 2, "b" }, { 6, "f" } };
    std::vector<std::pair<KT, MT>> src{ { 1, "a" }, { 4, "d" }, { 8, "h" } };
    m.insert( psi::vm::sorted_unique, src.begin(), src.end() );
    EXPECT_EQ( m.size(), 5u );
    EXPECT_TRUE( std::is_sorted( m.keys().begin(), m.keys().end() ) );
}

TYPED_TEST( flat_map_typed, insert_sorted_unique_initializer_list )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    map_t m{ { 2, "b" } };
    m.insert( psi::vm::sorted_unique, { std::pair<KT, MT>{ 1, "a" }, std::pair<KT, MT>{ 3, "c" } } );
    EXPECT_EQ( m.size(), 3u );
    EXPECT_EQ( m.at( 1 ), "a" );
}

////////////////////////////////////////////////////////////////////////////////
// erase_if
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( flat_map_typed, erase_if_basic )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 2, "b" }, { 3, "c" }, { 4, "d" }, { 5, "e" } };
    auto const erased{ erase_if( m, []( auto const & kv ) { return kv.first % 2 == 0; } ) };
    EXPECT_EQ( erased, 2u );
    EXPECT_EQ( m.size(), 3u );
    EXPECT_TRUE ( m.contains( 1 ) );
    EXPECT_FALSE( m.contains( 2 ) );
    EXPECT_TRUE ( m.contains( 3 ) );
    EXPECT_FALSE( m.contains( 4 ) );
    EXPECT_TRUE ( m.contains( 5 ) );
}

TYPED_TEST( flat_map_typed, erase_if_all )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 2, "b" } };
    auto const erased{ erase_if( m, []( auto const & ) { return true; } ) };
    EXPECT_EQ( erased, 2u );
    EXPECT_TRUE( m.empty() );
}

TYPED_TEST( flat_map_typed, erase_if_none )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 2, "b" } };
    auto const erased{ erase_if( m, []( auto const & ) { return false; } ) };
    EXPECT_EQ( erased, 0u );
    EXPECT_EQ( m.size(), 2u );
}

TYPED_TEST( flat_map_typed, erase_if_preserves_order )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 2, "b" }, { 3, "c" }, { 4, "d" }, { 5, "e" } };
    erase_if( m, []( auto const & kv ) { return kv.second == "c"; } );
    EXPECT_TRUE( std::is_sorted( m.keys().begin(), m.keys().end() ) );
    auto const & k{ m.keys() };
    ASSERT_EQ( k.size(), 4u );
    EXPECT_EQ( k[ 0 ], 1 );
    EXPECT_EQ( k[ 1 ], 2 );
    EXPECT_EQ( k[ 2 ], 4 );
    EXPECT_EQ( k[ 3 ], 5 );
}

////////////////////////////////////////////////////////////////////////////////
// Iterators
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( flat_map_typed, iterator_random_access )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 2, "b" }, { 3, "c" } };
    auto it{ m.begin() };
    EXPECT_EQ( ( it + 2 )->first, 3 );
    EXPECT_EQ( it[ 1 ].first, 2 );
    EXPECT_EQ( m.end() - m.begin(), 3 );
    EXPECT_LT( m.begin(), m.end() );
}

TYPED_TEST( flat_map_typed, iterator_structured_bindings )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 2, "b" }, { 3, "c" } };
    int expectedKeys[]{ 1, 2, 3 };
    std::string_view expectedVals[]{ "a", "b", "c" };
    std::size_t i{ 0 };
    for ( auto [k, v] : m ) {
        EXPECT_EQ( k, expectedKeys[ i ] );
        EXPECT_EQ( v, expectedVals[ i ] );
        ++i;
    }
    EXPECT_EQ( i, 3u );
}

TYPED_TEST( flat_map_typed, iterator_mutable_value )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 2, "b" } };
    auto it{ m.find( 1 ) };
    it->second = "X";
    EXPECT_EQ( m.at( 1 ), "X" );
}

TYPED_TEST( flat_map_typed, iterator_address_stability )
{
    using map_t = typename TypeParam::map_type;
    using MT    = typename TypeParam::mapped_type;
    map_t m{ { 1, "a" }, { 2, "b" }, { 3, "c" } };
    auto it{ m.find( 2 ) };
    MT * addr{ &( it->second ) };
    EXPECT_EQ( *addr, "b" );
    *addr = "X";
    EXPECT_EQ( m.at( 2 ), "X" );
}

TYPED_TEST( flat_map_typed, const_iterator )
{
    using map_t = typename TypeParam::map_type;
    map_t const m{ { 1, "a" }, { 2, "b" } };
    auto it{ m.begin() };
    EXPECT_EQ( it->first, 1 );
    EXPECT_EQ( it->second, "a" );
}

TYPED_TEST( flat_map_typed, reverse_iterator )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 2, "b" }, { 3, "c" } };
    auto rit{ m.rbegin() };
    EXPECT_EQ( rit->first, 3 );
    ++rit;
    EXPECT_EQ( rit->first, 2 );
}

// values() returns std::span — only works with contiguous containers (not deque)
// Kept as standalone test below (values_in_key_order)

////////////////////////////////////////////////////////////////////////////////
// Edge cases
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( flat_map_typed, empty_map_operations )
{
    using map_t = typename TypeParam::map_type;
    map_t m;
    EXPECT_EQ( m.find( 1 ), m.end() );
    EXPECT_FALSE( m.contains( 1 ) );
    EXPECT_EQ( m.count( 1 ), 0u );
    EXPECT_EQ( m.erase( 1 ), 0u );
    EXPECT_EQ( m.lower_bound( 1 ), m.end() );
    EXPECT_EQ( m.upper_bound( 1 ), m.end() );
}

TYPED_TEST( flat_map_typed, single_element )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 42, "x" } };
    EXPECT_EQ( m.size(), 1u );
    EXPECT_EQ( m.at( 42 ), "x" );
    EXPECT_EQ( m.begin()->first, 42 );
    EXPECT_EQ( m.end() - m.begin(), 1 );
    m.erase( m.begin() );
    EXPECT_TRUE( m.empty() );
}

TYPED_TEST( flat_map_typed, duplicate_key_insertion )
{
    using map_t = typename TypeParam::map_type;
    map_t m;
    m.try_emplace( 1, "a" );
    m.try_emplace( 1, "b" );
    m.try_emplace( 1, "c" );
    EXPECT_EQ( m.size(), 1u );
    EXPECT_EQ( m.at( 1 ), "a" ); // first insertion wins
}

////////////////////////////////////////////////////////////////////////////////
// Misc
////////////////////////////////////////////////////////////////////////////////

TYPED_TEST( flat_map_typed, swap )
{
    using map_t = typename TypeParam::map_type;
    map_t a{ { 1, "a" } };
    map_t b{ { 2, "b" }, { 3, "c" } };
    a.swap( b );
    EXPECT_EQ( a.size(), 2u );
    EXPECT_EQ( b.size(), 1u );
    EXPECT_EQ( a.at( 2 ), "b" );
    EXPECT_EQ( b.at( 1 ), "a" );
}

TYPED_TEST( flat_map_typed, clear )
{
    using map_t = typename TypeParam::map_type;
    map_t m{ { 1, "a" }, { 2, "b" } };
    m.clear();
    EXPECT_TRUE( m.empty() );
    EXPECT_EQ( m.size(), 0u );
    // Should be usable again
    m[ 3 ] = "c";
    EXPECT_EQ( m.at( 3 ), "c" );
}

TYPED_TEST( flat_map_typed, comparison )
{
    using map_t = typename TypeParam::map_type;
    map_t a{ { 1, "a" }, { 2, "b" } };
    map_t b{ { 1, "a" }, { 2, "b" } };
    map_t c{ { 1, "a" }, { 3, "c" } };
    EXPECT_EQ( a, b );
    EXPECT_NE( a, c );
}

TYPED_TEST( flat_map_typed, initializer_list_assignment )
{
    using map_t = typename TypeParam::map_type;
    using KT    = typename TypeParam::key_type;
    using MT    = typename TypeParam::mapped_type;
    map_t m{ { 1, "a" } };
    m = { std::pair<KT, MT>{ 2, "b" }, std::pair<KT, MT>{ 3, "c" } };
    EXPECT_EQ( m.size(), 2u );
    EXPECT_FALSE( m.contains( 1 ) );
    EXPECT_EQ( m.at( 2 ), "b" );
}


////////////////////////////////////////////////////////////////////////////////
// Standalone type aliases
////////////////////////////////////////////////////////////////////////////////

using FM             = flat_map<int, int>;
using tr_flat_map_ii = flat_map<int, int, std::less<int>, tr_vector<int>, tr_vector<int>>;
using tr_vec_map     = flat_map<int, int, std::less<int>, tr_vector<int, std::uint32_t>, tr_vector<int, std::uint32_t>>;
using tr_less_map     = flat_map<int, int, std::less<>>;
using tr_vec_less_map = flat_map<int, int, std::less<>, tr_vector<int>, tr_vector<int>>;


////////////////////////////////////////////////////////////////////////////////
// Numeric/large tests (need <int,int> for arithmetic)
////////////////////////////////////////////////////////////////////////////////

TEST( flat_map, emplace_hint_sorted_input )
{
    FM m;
    for ( int i{ 0 }; i < 100; ++i ) {
        m.emplace_hint( m.end(), i, i * 10 );
    }
    EXPECT_EQ( m.size(), 100u );
    for ( int i{ 0 }; i < 100; ++i ) {
        EXPECT_EQ( m.at( i ), i * 10 );
    }
}

TEST( flat_map, large_insert_range )
{
    FM m;
    std::vector<std::pair<int, int>> src;
    src.reserve( 1000 );
    for ( int i{ 999 }; i >= 0; --i )
        src.emplace_back( i, i * 10 );
    m.insert_range( src );
    EXPECT_EQ( m.size(), 1000u );
    EXPECT_TRUE( std::is_sorted( m.keys().begin(), m.keys().end() ) );
    EXPECT_EQ( m.at( 0 ), 0 );
    EXPECT_EQ( m.at( 999 ), 9990 );
}

TEST( flat_map, large_merge_bulk )
{
    FM target;
    for ( int i{ 0 }; i < 500; i += 2 )
        target.try_emplace( i, i );
    FM source;
    for ( int i{ 1 }; i < 500; i += 2 )
        source.try_emplace( i, i );
    target.merge( source );
    EXPECT_EQ( target.size(), 500u );
    EXPECT_TRUE( source.empty() );
    EXPECT_TRUE( std::is_sorted( target.keys().begin(), target.keys().end() ) );
}

////////////////////////////////////////////////////////////////////////////////
// Container-specific (use .data()/.reserve())
////////////////////////////////////////////////////////////////////////////////

TEST( flat_map, keys_returns_sorted_contiguous )
{
    FM m{ { 5, 50 }, { 1, 10 }, { 3, 30 } };
    auto const & keys{ m.keys() };
    ASSERT_EQ( keys.size(), 3u );
    EXPECT_EQ( keys[ 0 ], 1 );
    EXPECT_EQ( keys[ 1 ], 3 );
    EXPECT_EQ( keys[ 2 ], 5 );
    EXPECT_TRUE( std::is_sorted( keys.begin(), keys.end() ) );
    // Verify contiguous (can form span)
    std::span<int const> keySpan{ keys.data(), keys.size() };
    EXPECT_EQ( keySpan[ 1 ], 3 );
}

TEST( flat_map, values_in_key_order )
{
    flat_map<int, std::string> m{ { 3, "c" }, { 1, "a" }, { 2, "b" } };
    auto const & vals{ m.values() };
    EXPECT_EQ( vals[ 0 ], "a" ); // key=1
    EXPECT_EQ( vals[ 1 ], "b" ); // key=2
    EXPECT_EQ( vals[ 2 ], "c" ); // key=3
}

TEST( flat_map, tr_vector_keys_span )
{
    tr_flat_map_ii m;
    m[ 5 ] = 50; m[ 1 ] = 10; m[ 3 ] = 30;
    auto const & keys{ m.keys() };
    // tr_vector is contiguous — can form a span for cache-friendly key-only iteration
    std::span<int const> keySpan{ keys.data(), keys.size() };
    EXPECT_EQ( keySpan.size(), 3u );
    EXPECT_EQ( keySpan[ 0 ], 1 );
    EXPECT_EQ( keySpan[ 1 ], 3 );
    EXPECT_EQ( keySpan[ 2 ], 5 );
}

TEST( flat_map, reserve_and_shrink )
{
    FM m;
    m.reserve( 100 );
    for ( int i{ 0 }; i < 50; ++i ) {
        m.emplace_hint( m.end(), i, i * 10 );
    }
    EXPECT_EQ( m.size(), 50u );
    m.shrink_to_fit();
    EXPECT_EQ( m.size(), 50u );
}

TEST( flat_map, tr_vector_reserve_and_shrink )
{
    tr_flat_map_ii m;
    m.reserve( 100 );
    for ( int i{ 0 }; i < 50; ++i ) {
        m.emplace_hint( m.end(), i, i * 10 );
    }
    EXPECT_EQ( m.size(), 50u );
    m.shrink_to_fit();
    EXPECT_EQ( m.size(), 50u );
    EXPECT_EQ( m.at( 25 ), 250 );
}

////////////////////////////////////////////////////////////////////////////////
// Transparent comparator (std::less<>)
////////////////////////////////////////////////////////////////////////////////

TEST( flat_map, transparent_comparison )
{
    flat_map<int, std::string, std::less<>> m{ { 1, "a" }, { 3, "c" } };
    EXPECT_TRUE( m.contains( 1L ) );
    EXPECT_NE  ( m.find( 3L ), m.end() );
    auto lb{ m.lower_bound( 2L ) };
    EXPECT_EQ( lb->first, 3 );
}

TEST( flat_map, transparent_find_with_various_types )
{
    tr_less_map m{ { 1, 10 }, { 3, 30 }, { 5, 50 } };

    // Find with long
    auto it1{ m.find( 3L ) };
    ASSERT_NE( it1, m.end() );
    EXPECT_EQ( it1->second, 30 );

    // Find with unsigned
    auto it2{ m.find( 5U ) };
    ASSERT_NE( it2, m.end() );
    EXPECT_EQ( it2->second, 50 );

    // Find with short
    auto it3{ m.find( static_cast<short>( 1 ) ) };
    ASSERT_NE( it3, m.end() );
    EXPECT_EQ( it3->second, 10 );

    // Miss
    EXPECT_EQ( m.find( 2L ), m.end() );
}

TEST( flat_map, transparent_lower_upper_bound )
{
    tr_less_map m{ { 10, 100 }, { 20, 200 }, { 30, 300 } };

    auto lb{ m.lower_bound( 15L ) };
    EXPECT_EQ( lb->first, 20 );

    auto ub{ m.upper_bound( 20L ) };
    EXPECT_EQ( ub->first, 30 );

    auto [lo, hi]{ m.equal_range( 20U ) };
    EXPECT_EQ( lo->first, 20 );
    EXPECT_EQ( hi->first, 30 );
}

TEST( flat_map, transparent_contains_count )
{
    tr_less_map m{ { 1, 10 }, { 3, 30 } };
    EXPECT_TRUE ( m.contains( 1L  ) );
    EXPECT_TRUE ( m.contains( 3U  ) );
    EXPECT_FALSE( m.contains( 2L  ) );
    EXPECT_EQ   ( m.count( 1L  ), 1u );
    EXPECT_EQ   ( m.count( 99U ), 0u );
}

TEST( flat_map, tr_vector_transparent_combined )
{
    tr_vec_less_map m;
    m[ 5 ] = 50; m[ 1 ] = 10; m[ 3 ] = 30;

    // Heterogeneous lookup
    EXPECT_TRUE ( m.contains( 3L  ) );
    EXPECT_TRUE ( m.contains( 5U  ) );
    EXPECT_FALSE( m.contains( 2L  ) );

    auto it{ m.find( 1L ) };
    ASSERT_NE( it, m.end() );
    EXPECT_EQ( it->second, 10 );

    auto lb{ m.lower_bound( 2U ) };
    EXPECT_EQ( lb->first, 3 );

    // Keys span — the actual use case for DimMembersSpan
    auto const & keys{ m.keys() };
    std::span<int const> keySpan{ keys.data(), keys.size() };
    EXPECT_EQ( keySpan[ 0 ], 1 );
    EXPECT_EQ( keySpan[ 1 ], 3 );
    EXPECT_EQ( keySpan[ 2 ], 5 );
}

TEST( flat_map, tr_vector_transparent_emplace_hint_and_erase )
{
    tr_vec_less_map m;
    for ( int i{ 0 }; i < 50; ++i ) {
        m.emplace_hint( m.end(), i, i * 10 );
    }
    EXPECT_EQ( m.size(), 50u );

    // Heterogeneous find + erase
    auto it{ m.find( 25L ) };
    ASSERT_NE( it, m.end() );
    m.erase( it );
    EXPECT_EQ( m.size(), 49u );
    EXPECT_FALSE( m.contains( 25U ) );

    // Verify sorted invariant
    auto const & keys{ m.keys() };
    EXPECT_TRUE( std::is_sorted( keys.begin(), keys.end() ) );
}

TEST( flat_map, transparent_at )
{
    using less_map = psi::vm::flat_map<std::string, int, std::less<>>;
    less_map m;
    m.try_emplace( "alpha", 1 );
    m.try_emplace( "beta", 2 );

    // Transparent at with string_view
    std::string_view sv{ "alpha" };
    EXPECT_EQ( m.at( sv ), 1 );

    auto const & cm{ m };
    EXPECT_EQ( cm.at( sv ), 2 - 1 ); // const version

    EXPECT_THROW( m.at( std::string_view{ "gamma" } ), std::out_of_range );
}

TEST( flat_map, transparent_operator_bracket )
{
    using less_map = psi::vm::flat_map<std::string, int, std::less<>>;
    less_map m;
    m.try_emplace( "x", 42 );

    std::string_view sv{ "x" };
    EXPECT_EQ( m[ sv ], 42 );

    // Insert via transparent operator[]
    std::string_view sv2{ "y" };
    m[ sv2 ] = 99;
    EXPECT_EQ( m.at( "y" ), 99 );
}

TEST( flat_map, transparent_erase )
{
    using less_map = psi::vm::flat_map<std::string, int, std::less<>>;
    less_map m;
    m.try_emplace( "a", 1 );
    m.try_emplace( "b", 2 );
    m.try_emplace( "c", 3 );

    auto const erased{ m.erase( std::string_view{ "b" } ) };
    EXPECT_EQ( erased, 1u );
    EXPECT_EQ( m.size(), 2u );
    EXPECT_FALSE( m.contains( std::string_view{ "b" } ) );
}

////////////////////////////////////////////////////////////////////////////////
// CTAD
////////////////////////////////////////////////////////////////////////////////

TEST( flat_map, deduction_guide_initializer_list )
{
    psi::vm::flat_map m{ std::pair{ 1, 2 }, std::pair{ 3, 4 } };
    static_assert( std::is_same_v<typename decltype( m )::key_type, int> );
    static_assert( std::is_same_v<typename decltype( m )::mapped_type, int> );
    EXPECT_EQ( m.size(), 2u );
}

TEST( flat_map, deduction_guide_sorted_unique_initializer_list )
{
    psi::vm::flat_map m( psi::vm::sorted_unique, { std::pair{ 1, 10 }, std::pair{ 2, 20 } } );
    EXPECT_EQ( m.size(), 2u );
    EXPECT_EQ( m.at( 1 ), 10 );
}

TEST( flat_map, deduction_guide_container_pair )
{
    std::vector<int> k{ 3, 1, 2 };
    std::vector<double> v{ 3.0, 1.0, 2.0 };
    psi::vm::flat_map m( std::move( k ), std::move( v ) );
    static_assert( std::is_same_v<typename decltype( m )::key_type, int> );
    static_assert( std::is_same_v<typename decltype( m )::mapped_type, double> );
    EXPECT_EQ( m.size(), 3u );
    EXPECT_DOUBLE_EQ( m.at( 1 ), 1.0 );
}

TEST( flat_map, deduction_guide_iterator_range )
{
    std::vector<std::pair<int, double>> src{ { 1, 1.0 }, { 2, 2.0 } };
    psi::vm::flat_map m( src.begin(), src.end() );
    static_assert( std::is_same_v<typename decltype( m )::key_type, int> );
    static_assert( std::is_same_v<typename decltype( m )::mapped_type, double> );
    EXPECT_EQ( m.size(), 2u );
}

////////////////////////////////////////////////////////////////////////////////
// Size type
////////////////////////////////////////////////////////////////////////////////

TEST( flat_map, size_type_matches_smaller_container )
{
    static_assert( std::is_same_v<FM::size_type, std::size_t> );
}

TEST( flat_map, tr_vector_size_type_is_uint32 )
{
    static_assert( std::is_same_v<tr_vec_map::size_type, std::uint32_t> );
}

TEST( flat_map, tr_vector_insert_range )
{
    tr_vec_map m;
    m.try_emplace( 1, 10 );
    m.try_emplace( 5, 50 );
    std::vector<std::pair<int, int>> src{ { 3, 30 }, { 2, 20 }, { 4, 40 } };
    m.insert_range( src );
    EXPECT_EQ( m.size(), 5u );
    for ( int i{ 1 }; i <= 5; ++i )
        EXPECT_EQ( m.at( i ), i * 10 );
}

TEST( flat_map, tr_vector_erase_if )
{
    tr_vec_map m;
    for ( int i{ 0 }; i < 10; ++i )
        m.try_emplace( i, i * 10 );
    auto const erased{ erase_if( m, []( auto const & kv ) { return kv.first >= 5; } ) };
    EXPECT_EQ( erased, 5u );
    EXPECT_EQ( m.size(), 5u );
}

TEST( flat_map, tr_vector_bulk_merge )
{
    tr_vec_map target;
    target.try_emplace( 1, 10 );
    target.try_emplace( 3, 30 );
    tr_vec_map source;
    source.try_emplace( 2, 20 );
    source.try_emplace( 3, 999 );
    target.merge( source );
    EXPECT_EQ( target.size(), 3u );
    EXPECT_EQ( target.at( 3 ), 30 ); // existing wins
    EXPECT_EQ( source.size(), 1u );   // duplicate stays in source
}

////////////////////////////////////////////////////////////////////////////////
// Forwarding/copy-tracking
////////////////////////////////////////////////////////////////////////////////

// Move/copy tracking type for forwarding correctness tests
struct tracked
{
    int  value{ -1 };
    int  copies{ 0 };
    int  moves { 0 };

    tracked() = default;
    explicit tracked( int v ) : value{ v } {}

    tracked( tracked const & o ) : value{ o.value }, copies{ o.copies + 1 }, moves{ o.moves } {}
    tracked( tracked &&      o ) noexcept : value{ o.value }, copies{ o.copies }, moves{ o.moves + 1 } { o.value = -1; }

    tracked & operator=( tracked const & o ) { value = o.value; copies = o.copies + 1; moves = o.moves; return *this; }
    tracked & operator=( tracked &&      o ) noexcept { value = o.value; copies = o.copies; moves = o.moves + 1; o.value = -1; return *this; }

    bool operator< ( tracked const & o ) const noexcept { return value <  o.value; }
    bool operator==( tracked const & o ) const noexcept { return value == o.value; }
};

TEST( flat_map, forwarding_lvalue_insert_copies_not_moves )
{
    // append_from with lvalue iterators: std::get<N>(forward<pair<T,T>&>(elem)) -> copy
    std::vector<std::pair<tracked, tracked>> src{
        { tracked{ 3 }, tracked{ 30 } },
        { tracked{ 1 }, tracked{ 10 } },
        { tracked{ 2 }, tracked{ 20 } }
    };
    // Reset counters (vector emplacement may have copied/moved)
    for ( auto & [k, v] : src ) { k.copies = k.moves = v.copies = v.moves = 0; }

    flat_map<tracked, tracked> m;
    m.insert( src.begin(), src.end() ); // lvalue iterators -> append_from

    EXPECT_EQ( m.size(), 3u );
    // Source must be intact (copied, not moved)
    for ( auto const & [k, v] : src ) {
        EXPECT_NE( k.value, -1 ) << "key was moved from";
        EXPECT_NE( v.value, -1 ) << "value was moved from";
    }
    // Each element in the map should have been copied (>=1) and never moved from source
    for ( auto it{ m.begin() }; it != m.end(); ++it ) {
        EXPECT_GT( it->first.copies, 0 ) << "key should have been copied";
        EXPECT_GT( it->second.copies, 0 ) << "value should have been copied";
    }
}

// libstdc++ bug: ranges::sort on zip_view delegates to std::sort which uses
// std::move(*it) for temporaries. For proxy references (zip_view tuples of
// lvalue refs), reference collapsing (T& && -> T&) causes copies instead of moves.
// libc++ correctly uses ranges::iter_move via _RangeAlgPolicy, which dispatches
// through ADL to the proxy iterator's custom iter_move -- no copies.
// Confirmed: libc++ passes these tests, libstdc++ (GCC 15.2.1) does not.
#ifndef __GLIBCXX__
TEST( flat_map, forwarding_move_iterator_insert_moves_not_copies )
{
    // append_from with move iterators: std::get<N>(forward<pair<T,T>&&>(elem)) -> move
    std::vector<std::pair<tracked, tracked>> src{
        { tracked{ 2 }, tracked{ 20 } },
        { tracked{ 1 }, tracked{ 10 } }
    };
    for ( auto & [k, v] : src ) { k.copies = k.moves = v.copies = v.moves = 0; }

    flat_map<tracked, tracked> m;
    m.insert( std::make_move_iterator( src.begin() ), std::make_move_iterator( src.end() ) );

    EXPECT_EQ( m.size(), 2u );
    // Source should have been moved from
    for ( auto const & [k, v] : src ) {
        EXPECT_EQ( k.value, -1 ) << "key was NOT moved from";
        EXPECT_EQ( v.value, -1 ) << "value was NOT moved from";
    }
    // Map elements: zero copies from source (moves only from emplace_back + possible sort moves)
    for ( auto it{ m.begin() }; it != m.end(); ++it ) {
        EXPECT_EQ( it->first.copies, 0 )  << "key should not have been copied from source";
        EXPECT_EQ( it->second.copies, 0 ) << "value should not have been copied from source";
        EXPECT_GT( it->first.moves, 0 )   << "key should have been moved";
        EXPECT_GT( it->second.moves, 0 )  << "value should have been moved";
    }
}
#endif

TEST( flat_map, forwarding_insert_range_lvalue_copies )
{
    // insert_range from lvalue range -> common view -> lvalue iterators -> copies
    std::vector<std::pair<tracked, tracked>> src{
        { tracked{ 1 }, tracked{ 10 } },
        { tracked{ 2 }, tracked{ 20 } }
    };
    for ( auto & [k, v] : src ) { k.copies = k.moves = v.copies = v.moves = 0; }

    flat_map<tracked, tracked> m;
    m.insert_range( src );

    EXPECT_EQ( m.size(), 2u );
    for ( auto const & [k, v] : src ) {
        EXPECT_NE( k.value, -1 ) << "key was moved from";
        EXPECT_NE( v.value, -1 ) << "value was moved from";
    }
}

TEST( flat_map, forwarding_constructor_lvalue_copies )
{
    // Range constructor with lvalue iterators -> append_from -> copies
    std::vector<std::pair<tracked, tracked>> src{
        { tracked{ 2 }, tracked{ 20 } },
        { tracked{ 1 }, tracked{ 10 } }
    };
    for ( auto & [k, v] : src ) { k.copies = k.moves = v.copies = v.moves = 0; }

    flat_map<tracked, tracked> m( src.begin(), src.end() );

    EXPECT_EQ( m.size(), 2u );
    for ( auto const & [k, v] : src ) {
        EXPECT_NE( k.value, -1 ) << "key was moved from";
        EXPECT_NE( v.value, -1 ) << "value was moved from";
    }
}

#ifndef __GLIBCXX__ // see comment above forwarding_move_iterator_insert_moves_not_copies
TEST( flat_map, forwarding_constructor_move_iterator_moves )
{
    // Range constructor with move iterators -> append_from -> moves
    std::vector<std::pair<tracked, tracked>> src{
        { tracked{ 2 }, tracked{ 20 } },
        { tracked{ 1 }, tracked{ 10 } }
    };
    for ( auto & [k, v] : src ) { k.copies = k.moves = v.copies = v.moves = 0; }

    flat_map<tracked, tracked> m( std::make_move_iterator( src.begin() ), std::make_move_iterator( src.end() ) );

    EXPECT_EQ( m.size(), 2u );
    for ( auto const & [k, v] : src ) {
        EXPECT_EQ( k.value, -1 ) << "key was NOT moved from";
        EXPECT_EQ( v.value, -1 ) << "value was NOT moved from";
    }
    for ( auto it{ m.begin() }; it != m.end(); ++it ) {
        EXPECT_EQ( it->first.copies, 0 )  << "key should not have been copied";
        EXPECT_EQ( it->second.copies, 0 ) << "value should not have been copied";
    }
}
#endif

TEST( flat_map, forwarding_sorted_unique_insert_lvalue_copies )
{
    // insert(sorted_unique, ...) with lvalue iterators -> append_from -> copies
    std::vector<std::pair<tracked, tracked>> src{
        { tracked{ 1 }, tracked{ 10 } },
        { tracked{ 2 }, tracked{ 20 } },
        { tracked{ 3 }, tracked{ 30 } }
    };
    for ( auto & [k, v] : src ) { k.copies = k.moves = v.copies = v.moves = 0; }

    flat_map<tracked, tracked> m;
    m.insert( sorted_unique, src.begin(), src.end() );

    EXPECT_EQ( m.size(), 3u );
    for ( auto const & [k, v] : src ) {
        EXPECT_NE( k.value, -1 ) << "key was moved from";
        EXPECT_NE( v.value, -1 ) << "value was moved from";
    }
}

TEST( flat_map, forwarding_merge_rvalue_moves )
{
    // merge(flat_map&&) -> append_move_containers -> moves
    flat_map<tracked, tracked> target;
    target.insert( { tracked{ 1 }, tracked{ 10 } } );
    target.insert( { tracked{ 3 }, tracked{ 30 } } );

    flat_map<tracked, tracked> source;
    source.insert( { tracked{ 2 }, tracked{ 20 } } );
    source.insert( { tracked{ 4 }, tracked{ 40 } } );

    // Reset counters after initial insertions
    for ( auto it{ target.begin() }; it != target.end(); ++it ) {
        const_cast<tracked &>( it->first ).copies = const_cast<tracked &>( it->first ).moves = 0;
        it->second.copies = it->second.moves = 0;
    }
    for ( auto it{ source.begin() }; it != source.end(); ++it ) {
        const_cast<tracked &>( it->first ).copies = const_cast<tracked &>( it->first ).moves = 0;
        it->second.copies = it->second.moves = 0;
    }

    target.merge( std::move( source ) );

    EXPECT_EQ( target.size(), 4u );
    EXPECT_TRUE( source.empty() );
    // The merged elements (2,4) should have been moved, not copied
    EXPECT_EQ( target.find( tracked{ 2 } )->first.copies,  0 ) << "merged key should not have been copied";
    EXPECT_EQ( target.find( tracked{ 2 } )->second.copies, 0 ) << "merged value should not have been copied";
    EXPECT_EQ( target.find( tracked{ 4 } )->first.copies,  0 ) << "merged key should not have been copied";
    EXPECT_EQ( target.find( tracked{ 4 } )->second.copies, 0 ) << "merged value should not have been copied";
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
