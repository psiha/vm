////////////////////////////////////////////////////////////////////////////////
/// psi::vm::flat_map unit tests
////////////////////////////////////////////////////////////////////////////////

#include <psi/vm/containers/flat_map.hpp>
#include <psi/vm/containers/tr_vector.hpp>
#include <psi/vm/containers/is_trivially_moveable.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm {
//------------------------------------------------------------------------------

//==============================================================================
// Construction
//==============================================================================

TEST( flat_map, default_construction )
{
    flat_map<int, std::string> m;
    EXPECT_TRUE ( m.empty() );
    EXPECT_EQ   ( m.size(), 0 );
    EXPECT_EQ   ( m.begin(), m.end() );
}

TEST( flat_map, sorted_unique_construction )
{
    std::vector<int>         keys  { 1, 3, 5, 7 };
    std::vector<std::string> values{ "a", "b", "c", "d" };
    flat_map<int, std::string> m( sorted_unique, std::move( keys ), std::move( values ) );

    EXPECT_EQ( m.size(), 4 );
    EXPECT_EQ( m.at( 1 ), "a" );
    EXPECT_EQ( m.at( 3 ), "b" );
    EXPECT_EQ( m.at( 5 ), "c" );
    EXPECT_EQ( m.at( 7 ), "d" );
}

TEST( flat_map, range_construction )
{
    std::vector<std::pair<int, std::string>> pairs{
        { 5, "e" }, { 1, "a" }, { 3, "c" }, { 1, "dup" }
    };
    flat_map<int, std::string> m( pairs.begin(), pairs.end() );

    EXPECT_EQ( m.size(), 3 );
    EXPECT_EQ( m.at( 1 ), "a" ); // first wins
    EXPECT_EQ( m.at( 3 ), "c" );
    EXPECT_EQ( m.at( 5 ), "e" );
}

TEST( flat_map, initializer_list_construction )
{
    flat_map<int, int> m{ { 3, 30 }, { 1, 10 }, { 2, 20 } };
    EXPECT_EQ( m.size(), 3 );
    EXPECT_EQ( m.at( 1 ), 10 );
    EXPECT_EQ( m.at( 2 ), 20 );
    EXPECT_EQ( m.at( 3 ), 30 );
}

TEST( flat_map, copy_construction )
{
    flat_map<int, int> src{ { 1, 10 }, { 2, 20 } };
    flat_map<int, int> dst{ src };
    EXPECT_EQ( dst.size(), 2 );
    EXPECT_EQ( dst.at( 1 ), 10 );
    EXPECT_EQ( dst.at( 2 ), 20 );
    // Verify independence
    src[ 1 ] = 99;
    EXPECT_EQ( dst.at( 1 ), 10 );
}

TEST( flat_map, move_construction )
{
    flat_map<int, int> src{ { 1, 10 }, { 2, 20 } };
    flat_map<int, int> dst{ std::move( src ) };
    EXPECT_EQ( dst.size(), 2 );
    EXPECT_EQ( dst.at( 1 ), 10 );
}

//==============================================================================
// Element access
//==============================================================================

TEST( flat_map, operator_bracket_insert_and_access )
{
    flat_map<int, int> m;
    m[ 3 ] = 30;
    m[ 1 ] = 10;
    m[ 2 ] = 20;
    EXPECT_EQ( m.size(), 3 );
    EXPECT_EQ( m[ 1 ], 10 );
    EXPECT_EQ( m[ 2 ], 20 );
    EXPECT_EQ( m[ 3 ], 30 );

    // operator[] on existing key doesn't insert
    m[ 1 ] = 99;
    EXPECT_EQ( m.size(), 3 );
    EXPECT_EQ( m[ 1 ], 99 );
}

TEST( flat_map, at_throws_on_missing )
{
    flat_map<int, int> m{ { 1, 10 } };
    EXPECT_EQ( m.at( 1 ), 10 );
    EXPECT_THROW( m.at( 2 ), std::out_of_range );
}

TEST( flat_map, const_at )
{
    flat_map<int, int> const m{ { 1, 10 }, { 2, 20 } };
    EXPECT_EQ( m.at( 1 ), 10 );
    EXPECT_THROW( m.at( 99 ), std::out_of_range );
}

//==============================================================================
// Insertion
//==============================================================================

TEST( flat_map, try_emplace )
{
    flat_map<int, std::string> m;
    auto [it1, ok1] = m.try_emplace( 2, "two" );
    EXPECT_TRUE ( ok1 );
    EXPECT_EQ   ( it1->first, 2 );
    EXPECT_EQ   ( it1->second, "two" );

    auto [it2, ok2] = m.try_emplace( 2, "TWO" );
    EXPECT_FALSE( ok2 );
    EXPECT_EQ   ( it2->second, "two" ); // not overwritten
}

TEST( flat_map, insert )
{
    flat_map<int, int> m;
    auto [it, ok] = m.insert( { 5, 50 } );
    EXPECT_TRUE( ok );
    EXPECT_EQ  ( it->first, 5 );
    EXPECT_EQ  ( it->second, 50 );

    auto [it2, ok2] = m.insert( { 5, 99 } );
    EXPECT_FALSE( ok2 );
    EXPECT_EQ   ( it2->second, 50 );
}

TEST( flat_map, insert_or_assign )
{
    flat_map<int, int> m;
    auto [it1, ok1] = m.insert_or_assign( 1, 10 );
    EXPECT_TRUE( ok1 );
    EXPECT_EQ  ( it1->second, 10 );

    auto [it2, ok2] = m.insert_or_assign( 1, 99 );
    EXPECT_FALSE( ok2 );
    EXPECT_EQ   ( it2->second, 99 ); // overwritten
}

TEST( flat_map, emplace )
{
    flat_map<int, std::string> m;
    auto [it, ok] = m.emplace( 3, "three" );
    EXPECT_TRUE( ok );
    EXPECT_EQ  ( it->first, 3 );
}

TEST( flat_map, emplace_hint_sorted_input )
{
    flat_map<int, int> m;
    // Insert in sorted order with end() hint — should be O(1) each
    for ( int i{ 0 }; i < 100; ++i ) {
        m.emplace_hint( m.end(), i, i * 10 );
    }
    EXPECT_EQ( m.size(), 100 );
    for ( int i{ 0 }; i < 100; ++i ) {
        EXPECT_EQ( m.at( i ), i * 10 );
    }
}

TEST( flat_map, emplace_hint_wrong_hint )
{
    flat_map<int, int> m{ { 1, 10 }, { 5, 50 }, { 9, 90 } };
    // Wrong hint: insert 3 with hint at begin (should be between 1 and 5)
    auto it = m.emplace_hint( m.begin(), 3, 30 );
    EXPECT_EQ( it->first, 3 );
    EXPECT_EQ( it->second, 30 );
    EXPECT_EQ( m.size(), 4 );
    // Verify sorted order
    auto const & keys = m.keys();
    EXPECT_TRUE( std::is_sorted( keys.begin(), keys.end() ) );
}

TEST( flat_map, emplace_hint_existing_key )
{
    flat_map<int, int> m{ { 1, 10 }, { 5, 50 } };
    auto it = m.emplace_hint( m.begin() + 1, 5, 99 );
    EXPECT_EQ( it->first , 5 );
    EXPECT_EQ( it->second, 50 ); // not overwritten
    EXPECT_EQ( m.size(), 2 );
}

//==============================================================================
// Lookup
//==============================================================================

TEST( flat_map, find_hit_and_miss )
{
    flat_map<int, int> m{ { 1, 10 }, { 3, 30 }, { 5, 50 } };
    auto it = m.find( 3 );
    ASSERT_NE( it, m.end() );
    EXPECT_EQ( it->second, 30 );

    EXPECT_EQ( m.find( 2 ), m.end() );
    EXPECT_EQ( m.find( 0 ), m.end() );
    EXPECT_EQ( m.find( 99 ), m.end() );
}

TEST( flat_map, lower_upper_bound )
{
    flat_map<int, int> m{ { 1, 10 }, { 3, 30 }, { 5, 50 } };
    auto lb = m.lower_bound( 3 );
    EXPECT_EQ( lb->first, 3 );
    auto ub = m.upper_bound( 3 );
    EXPECT_EQ( ub->first, 5 );

    auto lb2 = m.lower_bound( 2 );
    EXPECT_EQ( lb2->first, 3 ); // first element >= 2
}

TEST( flat_map, equal_range )
{
    flat_map<int, int> m{ { 1, 10 }, { 3, 30 }, { 5, 50 } };
    auto [lo, hi] = m.equal_range( 3 );
    EXPECT_EQ( lo->first, 3 );
    EXPECT_EQ( hi->first, 5 );
    EXPECT_EQ( hi - lo, 1 );
}

TEST( flat_map, contains_and_count )
{
    flat_map<int, int> m{ { 1, 10 }, { 3, 30 } };
    EXPECT_TRUE ( m.contains( 1 ) );
    EXPECT_FALSE( m.contains( 2 ) );
    EXPECT_EQ   ( m.count( 1 ), 1 );
    EXPECT_EQ   ( m.count( 2 ), 0 );
}

TEST( flat_map, transparent_comparison )
{
    // std::less<> has is_transparent
    flat_map<int, std::string, std::less<>> m{ { 1, "a" }, { 3, "c" } };
    // Should compile and work with different key types (e.g. long)
    EXPECT_TRUE( m.contains( 1L ) );
    EXPECT_NE  ( m.find( 3L ), m.end() );
    auto lb = m.lower_bound( 2L );
    EXPECT_EQ( lb->first, 3 );
}

//==============================================================================
// Erasure
//==============================================================================

TEST( flat_map, erase_by_key )
{
    flat_map<int, int> m{ { 1, 10 }, { 2, 20 }, { 3, 30 } };
    EXPECT_EQ( m.erase( 2 ), 1 );
    EXPECT_EQ( m.size(), 2 );
    EXPECT_FALSE( m.contains( 2 ) );

    EXPECT_EQ( m.erase( 99 ), 0 );
    EXPECT_EQ( m.size(), 2 );
}

TEST( flat_map, erase_by_iterator )
{
    flat_map<int, int> m{ { 1, 10 }, { 2, 20 }, { 3, 30 } };
    auto it = m.find( 2 );
    auto next = m.erase( it );
    EXPECT_EQ( m.size(), 2 );
    EXPECT_EQ( next->first, 3 );
}

TEST( flat_map, erase_range )
{
    flat_map<int, int> m{ { 1, 10 }, { 2, 20 }, { 3, 30 }, { 4, 40 } };
    auto first = m.find( 2 );
    auto last  = m.find( 4 );
    m.erase( first, last );
    EXPECT_EQ( m.size(), 2 );
    EXPECT_TRUE ( m.contains( 1 ) );
    EXPECT_FALSE( m.contains( 2 ) );
    EXPECT_FALSE( m.contains( 3 ) );
    EXPECT_TRUE ( m.contains( 4 ) );
}

//==============================================================================
// Extract and Replace
//==============================================================================

TEST( flat_map, extract_and_replace )
{
    flat_map<int, int> m{ { 1, 10 }, { 2, 20 }, { 3, 30 } };
    auto c = m.extract();
    EXPECT_TRUE( m.empty() );
    EXPECT_EQ( c.keys.size(), 3 );
    EXPECT_EQ( c.values.size(), 3 );

    // Modify extracted containers
    c.keys.push_back( 4 );
    c.values.push_back( 40 );

    m.replace( std::move( c.keys ), std::move( c.values ) );
    EXPECT_EQ( m.size(), 4 );
    EXPECT_EQ( m.at( 4 ), 40 );
}

//==============================================================================
// Keys and Values accessors
//==============================================================================

TEST( flat_map, keys_returns_sorted_contiguous )
{
    flat_map<int, int> m{ { 5, 50 }, { 1, 10 }, { 3, 30 } };
    auto const & keys = m.keys();
    ASSERT_EQ( keys.size(), 3 );
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
    auto const & vals = m.values();
    EXPECT_EQ( vals[ 0 ], "a" ); // key=1
    EXPECT_EQ( vals[ 1 ], "b" ); // key=2
    EXPECT_EQ( vals[ 2 ], "c" ); // key=3
}

//==============================================================================
// Merge
//==============================================================================

TEST( flat_map, merge_non_overlapping )
{
    flat_map<int, int> a{ { 1, 10 }, { 3, 30 } };
    flat_map<int, int> b{ { 2, 20 }, { 4, 40 } };
    a.merge( b );
    EXPECT_EQ( a.size(), 4 );
    EXPECT_TRUE( b.empty() );
    EXPECT_EQ( a.at( 2 ), 20 );
    EXPECT_EQ( a.at( 4 ), 40 );
}

TEST( flat_map, merge_overlapping )
{
    flat_map<int, int> a{ { 1, 10 }, { 3, 30 } };
    flat_map<int, int> b{ { 2, 20 }, { 3, 99 } };
    a.merge( b );
    EXPECT_EQ( a.size(), 3 );
    EXPECT_EQ( a.at( 3 ), 30 ); // original kept
    EXPECT_EQ( b.size(), 1 );   // conflicting element remains in source
    EXPECT_EQ( b.at( 3 ), 99 );
}

//==============================================================================
// Iterator
//==============================================================================

TEST( flat_map, iterator_random_access )
{
    flat_map<int, int> m{ { 1, 10 }, { 2, 20 }, { 3, 30 } };
    auto it = m.begin();
    EXPECT_EQ( ( it + 2 )->first, 3 );
    EXPECT_EQ( it[ 1 ].first, 2 );
    EXPECT_EQ( m.end() - m.begin(), 3 );
    EXPECT_LT( m.begin(), m.end() );
}

TEST( flat_map, iterator_structured_bindings )
{
    flat_map<int, int> m{ { 1, 10 }, { 2, 20 } };
    for ( auto [key, val] : m ) {
        EXPECT_EQ( val, key * 10 );
    }
}

TEST( flat_map, iterator_mutable_value )
{
    flat_map<int, int> m{ { 1, 10 }, { 2, 20 } };
    auto it = m.find( 1 );
    it->second = 99;
    EXPECT_EQ( m.at( 1 ), 99 );
}

TEST( flat_map, iterator_arrow_proxy_address_stability )
{
    // Verify that &(it->second) yields a stable address into the values container
    flat_map<int, int> m{ { 1, 10 }, { 2, 20 }, { 3, 30 } };
    auto it = m.find( 2 );
    int * addr = &( it->second );
    EXPECT_EQ( *addr, 20 );
    *addr = 99;
    EXPECT_EQ( m.at( 2 ), 99 );
}

TEST( flat_map, const_iterator )
{
    flat_map<int, int> const m{ { 1, 10 }, { 2, 20 } };
    auto it = m.begin();
    EXPECT_EQ( it->first, 1 );
    EXPECT_EQ( it->second, 10 );
    // Should NOT compile: it->second = 99;
}

TEST( flat_map, reverse_iterator )
{
    flat_map<int, int> m{ { 1, 10 }, { 2, 20 }, { 3, 30 } };
    auto rit = m.rbegin();
    EXPECT_EQ( rit->first, 3 );
    ++rit;
    EXPECT_EQ( rit->first, 2 );
}

//==============================================================================
// Edge cases
//==============================================================================

TEST( flat_map, empty_map_operations )
{
    flat_map<int, int> m;
    EXPECT_EQ( m.find( 1 ), m.end() );
    EXPECT_FALSE( m.contains( 1 ) );
    EXPECT_EQ( m.count( 1 ), 0 );
    EXPECT_EQ( m.erase( 1 ), 0 );
    EXPECT_EQ( m.lower_bound( 1 ), m.end() );
    EXPECT_EQ( m.upper_bound( 1 ), m.end() );
}

TEST( flat_map, single_element )
{
    flat_map<int, int> m{ { 42, 420 } };
    EXPECT_EQ( m.size(), 1 );
    EXPECT_EQ( m.at( 42 ), 420 );
    EXPECT_EQ( m.begin()->first, 42 );
    EXPECT_EQ( m.end() - m.begin(), 1 );
    m.erase( m.begin() );
    EXPECT_TRUE( m.empty() );
}

TEST( flat_map, duplicate_key_insertion )
{
    flat_map<int, int> m;
    m.try_emplace( 1, 10 );
    m.try_emplace( 1, 20 );
    m.try_emplace( 1, 30 );
    EXPECT_EQ( m.size(), 1 );
    EXPECT_EQ( m.at( 1 ), 10 ); // first insertion wins
}

TEST( flat_map, swap )
{
    flat_map<int, int> a{ { 1, 10 } };
    flat_map<int, int> b{ { 2, 20 }, { 3, 30 } };
    a.swap( b );
    EXPECT_EQ( a.size(), 2 );
    EXPECT_EQ( b.size(), 1 );
    EXPECT_EQ( a.at( 2 ), 20 );
    EXPECT_EQ( b.at( 1 ), 10 );
}

TEST( flat_map, clear )
{
    flat_map<int, int> m{ { 1, 10 }, { 2, 20 } };
    m.clear();
    EXPECT_TRUE( m.empty() );
    EXPECT_EQ( m.size(), 0 );
    // Should be usable again
    m[ 3 ] = 30;
    EXPECT_EQ( m.at( 3 ), 30 );
}

TEST( flat_map, comparison )
{
    flat_map<int, int> a{ { 1, 10 }, { 2, 20 } };
    flat_map<int, int> b{ { 1, 10 }, { 2, 20 } };
    flat_map<int, int> c{ { 1, 10 }, { 3, 30 } };
    EXPECT_EQ( a, b );
    EXPECT_NE( a, c );
}

TEST( flat_map, reserve_and_shrink )
{
    flat_map<int, int> m;
    m.reserve( 100 );
    for ( int i{ 0 }; i < 50; ++i ) {
        m.emplace_hint( m.end(), i, i * 10 );
    }
    EXPECT_EQ( m.size(), 50 );
    m.shrink_to_fit();
    EXPECT_EQ( m.size(), 50 );
}

//==============================================================================
// tr_vector containers
//==============================================================================

// flat_map using psi::vm::tr_vector as underlying storage
using tr_flat_map_ii = flat_map<int, int, std::less<int>, tr_vector<int>, tr_vector<int>>;

TEST( flat_map, tr_vector_basic )
{
    tr_flat_map_ii m;
    m[ 3 ] = 30;
    m[ 1 ] = 10;
    m[ 2 ] = 20;
    EXPECT_EQ( m.size(), 3 );
    EXPECT_EQ( m.at( 1 ), 10 );
    EXPECT_EQ( m.at( 2 ), 20 );
    EXPECT_EQ( m.at( 3 ), 30 );
}

TEST( flat_map, tr_vector_sorted_unique_construction )
{
    tr_vector<int> keys;
    keys.push_back( 1 ); keys.push_back( 3 ); keys.push_back( 5 );
    tr_vector<int> vals;
    vals.push_back( 10 ); vals.push_back( 30 ); vals.push_back( 50 );
    tr_flat_map_ii m( sorted_unique, std::move( keys ), std::move( vals ) );

    EXPECT_EQ( m.size(), 3 );
    EXPECT_EQ( m.at( 1 ), 10 );
    EXPECT_EQ( m.at( 3 ), 30 );
    EXPECT_EQ( m.at( 5 ), 50 );
}

TEST( flat_map, tr_vector_emplace_hint_sorted )
{
    tr_flat_map_ii m;
    for ( int i{ 0 }; i < 100; ++i ) {
        m.emplace_hint( m.end(), i, i * 10 );
    }
    EXPECT_EQ( m.size(), 100 );
    auto const & keys{ m.keys() };
    EXPECT_TRUE( std::is_sorted( keys.begin(), keys.end() ) );
    EXPECT_EQ( m.at( 42 ), 420 );
}

TEST( flat_map, tr_vector_extract_replace )
{
    tr_flat_map_ii m;
    m[ 1 ] = 10; m[ 2 ] = 20;
    auto c{ m.extract() };
    EXPECT_TRUE( m.empty() );
    EXPECT_EQ( c.keys.size(), 2 );
    c.keys.push_back( 3 );
    c.values.push_back( 30 );
    m.replace( std::move( c.keys ), std::move( c.values ) );
    EXPECT_EQ( m.size(), 3 );
    EXPECT_EQ( m.at( 3 ), 30 );
}

TEST( flat_map, tr_vector_keys_span )
{
    tr_flat_map_ii m;
    m[ 5 ] = 50; m[ 1 ] = 10; m[ 3 ] = 30;
    auto const & keys{ m.keys() };
    // tr_vector is contiguous — can form a span for cache-friendly key-only iteration
    std::span<int const> keySpan{ keys.data(), keys.size() };
    EXPECT_EQ( keySpan.size(), 3 );
    EXPECT_EQ( keySpan[ 0 ], 1 );
    EXPECT_EQ( keySpan[ 1 ], 3 );
    EXPECT_EQ( keySpan[ 2 ], 5 );
}

TEST( flat_map, tr_vector_erase_and_find )
{
    tr_flat_map_ii m;
    m[ 1 ] = 10; m[ 2 ] = 20; m[ 3 ] = 30;
    EXPECT_EQ( m.erase( 2 ), 1 );
    EXPECT_EQ( m.size(), 2 );
    EXPECT_EQ( m.find( 2 ), m.end() );
    EXPECT_NE( m.find( 1 ), m.end() );
    EXPECT_NE( m.find( 3 ), m.end() );
}

TEST( flat_map, tr_vector_merge )
{
    tr_flat_map_ii a;
    a[ 1 ] = 10; a[ 3 ] = 30;
    tr_flat_map_ii b;
    b[ 2 ] = 20; b[ 4 ] = 40;
    a.merge( b );
    EXPECT_EQ( a.size(), 4 );
    EXPECT_TRUE( b.empty() );
    EXPECT_EQ( a.at( 2 ), 20 );
}

TEST( flat_map, tr_vector_reserve_and_shrink )
{
    tr_flat_map_ii m;
    m.reserve( 100 );
    for ( int i{ 0 }; i < 50; ++i ) {
        m.emplace_hint( m.end(), i, i * 10 );
    }
    EXPECT_EQ( m.size(), 50 );
    m.shrink_to_fit();
    EXPECT_EQ( m.size(), 50 );
    EXPECT_EQ( m.at( 25 ), 250 );
}

//==============================================================================
// Transparent comparator (std::less<>)
//==============================================================================

// flat_map with std::less<> and std::vector — exercises heterogeneous lookup
using tr_less_map = flat_map<int, int, std::less<>>;

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
    EXPECT_EQ   ( m.count( 1L  ), 1 );
    EXPECT_EQ   ( m.count( 99U ), 0 );
}

// flat_map with std::less<> and tr_vector — the combination used in rama
using tr_vec_less_map = flat_map<int, int, std::less<>, tr_vector<int>, tr_vector<int>>;

TEST( flat_map, tr_vector_transparent_combined )
{
    tr_vec_less_map m;
    m[ 5 ] = 50; m[ 1 ] = 10; m[ 3 ] = 30;

    // Heterogeneous lookup
    EXPECT_TRUE( m.contains( 3L  ) );
    EXPECT_TRUE( m.contains( 5U  ) );
    EXPECT_FALSE( m.contains( 2L ) );

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
    EXPECT_EQ( m.size(), 50 );

    // Heterogeneous find + erase
    auto it{ m.find( 25L ) };
    ASSERT_NE( it, m.end() );
    m.erase( it );
    EXPECT_EQ( m.size(), 49 );
    EXPECT_FALSE( m.contains( 25U ) );

    // Verify sorted invariant
    auto const & keys{ m.keys() };
    EXPECT_TRUE( std::is_sorted( keys.begin(), keys.end() ) );
}

// Type aliases for new tests
using FM          = flat_map<int, int>;
using tr_vec_map  = flat_map<int, int, std::less<int>, tr_vector<int, std::uint32_t>, tr_vector<int, std::uint32_t>>;

//==============================================================================
// New feature tests: insert_range, bulk insert, merge rewrite, erase_if,
// from_range_t, deduction guides, transparent at/operator[], sorted_unique inserts
//==============================================================================

TEST( flat_map, insert_range_basic )
{
    FM m{ { 1, 10 }, { 3, 30 }, { 5, 50 } };
    std::vector<std::pair<int, int>> src{ { 2, 20 }, { 4, 40 }, { 6, 60 } };
    m.insert_range( src );
    EXPECT_EQ( m.size(), 6 );
    for ( int i{ 1 }; i <= 6; ++i )
        EXPECT_EQ( m.at( i ), i * 10 );
}

TEST( flat_map, insert_range_with_duplicates )
{
    FM m{ { 1, 10 }, { 3, 30 }, { 5, 50 } };
    std::vector<std::pair<int, int>> src{ { 3, 999 }, { 5, 888 }, { 7, 70 } };
    m.insert_range( src );
    EXPECT_EQ( m.size(), 4 );
    // Existing values win
    EXPECT_EQ( m.at( 3 ), 30 );
    EXPECT_EQ( m.at( 5 ), 50 );
    EXPECT_EQ( m.at( 7 ), 70 );
}

TEST( flat_map, insert_range_into_empty )
{
    FM m;
    std::vector<std::pair<int, int>> src{ { 5, 50 }, { 1, 10 }, { 3, 30 } };
    m.insert_range( src );
    EXPECT_EQ( m.size(), 3 );
    EXPECT_TRUE( std::is_sorted( m.keys().begin(), m.keys().end() ) );
    EXPECT_EQ( m.at( 1 ), 10 );
    EXPECT_EQ( m.at( 3 ), 30 );
    EXPECT_EQ( m.at( 5 ), 50 );
}

TEST( flat_map, insert_range_sorted_unique )
{
    FM m{ { 2, 20 }, { 4, 40 } };
    std::vector<std::pair<int, int>> src{ { 1, 10 }, { 3, 30 }, { 5, 50 } };
    m.insert_range( psi::vm::sorted_unique, src );
    EXPECT_EQ( m.size(), 5 );
    for ( int i{ 1 }; i <= 5; ++i )
        EXPECT_EQ( m.at( i ), i * 10 );
}

TEST( flat_map, insert_range_empty_range )
{
    FM m{ { 1, 10 } };
    std::vector<std::pair<int, int>> empty;
    m.insert_range( empty );
    EXPECT_EQ( m.size(), 1 );
}

TEST( flat_map, bulk_insert_range_iterator )
{
    // insert(InputIt, InputIt) now uses bulk algorithm
    FM m{ { 1, 10 }, { 5, 50 } };
    std::vector<std::pair<int, int>> src{ { 3, 30 }, { 2, 20 }, { 4, 40 }, { 1, 999 } };
    m.insert( src.begin(), src.end() );
    EXPECT_EQ( m.size(), 5 );
    EXPECT_EQ( m.at( 1 ), 10 ); // existing wins
    EXPECT_EQ( m.at( 2 ), 20 );
    EXPECT_EQ( m.at( 3 ), 30 );
}

TEST( flat_map, insert_sorted_unique_range )
{
    FM m{ { 2, 20 }, { 6, 60 } };
    std::vector<std::pair<int, int>> src{ { 1, 10 }, { 4, 40 }, { 8, 80 } };
    m.insert( psi::vm::sorted_unique, src.begin(), src.end() );
    EXPECT_EQ( m.size(), 5 );
    EXPECT_TRUE( std::is_sorted( m.keys().begin(), m.keys().end() ) );
}

TEST( flat_map, insert_sorted_unique_initializer_list )
{
    FM m{ { 2, 20 } };
    m.insert( psi::vm::sorted_unique, { { 1, 10 }, { 3, 30 } } );
    EXPECT_EQ( m.size(), 3 );
    EXPECT_EQ( m.at( 1 ), 10 );
}

TEST( flat_map, merge_lvalue_bulk )
{
    FM target{ { 1, 10 }, { 3, 30 }, { 5, 50 } };
    FM source{ { 2, 20 }, { 3, 999 }, { 4, 40 } };
    target.merge( source );
    // target gets 2 and 4, not 3 (already exists)
    EXPECT_EQ( target.size(), 5 );
    EXPECT_EQ( target.at( 1 ), 10 );
    EXPECT_EQ( target.at( 2 ), 20 );
    EXPECT_EQ( target.at( 3 ), 30 ); // original wins
    EXPECT_EQ( target.at( 4 ), 40 );
    EXPECT_EQ( target.at( 5 ), 50 );
    // source retains only the duplicate
    EXPECT_EQ( source.size(), 1 );
    EXPECT_TRUE( source.contains( 3 ) );
}

TEST( flat_map, merge_rvalue_bulk )
{
    FM target{ { 1, 10 }, { 3, 30 } };
    FM source{ { 2, 20 }, { 3, 999 }, { 4, 40 } };
    target.merge( std::move( source ) );
    EXPECT_EQ( target.size(), 4 );
    EXPECT_EQ( target.at( 3 ), 30 ); // existing wins
    EXPECT_EQ( target.at( 2 ), 20 );
    EXPECT_EQ( target.at( 4 ), 40 );
    // source is cleared
    EXPECT_TRUE( source.empty() );
}

TEST( flat_map, merge_empty_source )
{
    FM target{ { 1, 10 } };
    FM source;
    target.merge( source );
    EXPECT_EQ( target.size(), 1 );
}

TEST( flat_map, merge_empty_target )
{
    FM target;
    FM source{ { 1, 10 }, { 2, 20 } };
    target.merge( source );
    EXPECT_EQ( target.size(), 2 );
    EXPECT_TRUE( source.empty() );
}

TEST( flat_map, merge_self )
{
    FM m{ { 1, 10 }, { 2, 20 } };
    m.merge( m );
    EXPECT_EQ( m.size(), 2 ); // no change
}

TEST( flat_map, erase_if_basic )
{
    FM m{ { 1, 10 }, { 2, 20 }, { 3, 30 }, { 4, 40 }, { 5, 50 } };
    auto const erased{ erase_if( m, []( auto const & kv ) { return kv.first % 2 == 0; } ) };
    EXPECT_EQ( erased, 2 );
    EXPECT_EQ( m.size(), 3 );
    EXPECT_TRUE( m.contains( 1 ) );
    EXPECT_FALSE( m.contains( 2 ) );
    EXPECT_TRUE( m.contains( 3 ) );
    EXPECT_FALSE( m.contains( 4 ) );
    EXPECT_TRUE( m.contains( 5 ) );
}

TEST( flat_map, erase_if_all )
{
    FM m{ { 1, 10 }, { 2, 20 } };
    auto const erased{ erase_if( m, []( auto const & ) { return true; } ) };
    EXPECT_EQ( erased, 2 );
    EXPECT_TRUE( m.empty() );
}

TEST( flat_map, erase_if_none )
{
    FM m{ { 1, 10 }, { 2, 20 } };
    auto const erased{ erase_if( m, []( auto const & ) { return false; } ) };
    EXPECT_EQ( erased, 0 );
    EXPECT_EQ( m.size(), 2 );
}

TEST( flat_map, erase_if_preserves_order )
{
    FM m{ { 1, 10 }, { 2, 20 }, { 3, 30 }, { 4, 40 }, { 5, 50 } };
    erase_if( m, []( auto const & kv ) { return kv.second == 30; } );
    EXPECT_TRUE( std::is_sorted( m.keys().begin(), m.keys().end() ) );
    auto const & k{ m.keys() };
    ASSERT_EQ( k.size(), 4 );
    EXPECT_EQ( k[ 0 ], 1 );
    EXPECT_EQ( k[ 1 ], 2 );
    EXPECT_EQ( k[ 2 ], 4 );
    EXPECT_EQ( k[ 3 ], 5 );
}

TEST( flat_map, from_range_t_construction )
{
    std::vector<std::pair<int, int>> src{ { 3, 30 }, { 1, 10 }, { 2, 20 } };
    FM m( std::from_range, src );
    EXPECT_EQ( m.size(), 3 );
    EXPECT_TRUE( std::is_sorted( m.keys().begin(), m.keys().end() ) );
    EXPECT_EQ( m.at( 1 ), 10 );
    EXPECT_EQ( m.at( 2 ), 20 );
    EXPECT_EQ( m.at( 3 ), 30 );
}

TEST( flat_map, from_range_t_with_duplicates )
{
    std::vector<std::pair<int, int>> src{ { 1, 10 }, { 1, 99 }, { 2, 20 } };
    FM m( std::from_range, src );
    EXPECT_EQ( m.size(), 2 );
    EXPECT_EQ( m.at( 1 ), 10 ); // first occurrence wins
}

TEST( flat_map, unsorted_container_construction )
{
    std::vector<int> keys{ 3, 1, 2 };
    std::vector<int> vals{ 30, 10, 20 };
    FM m( std::move( keys ), std::move( vals ) );
    EXPECT_EQ( m.size(), 3 );
    EXPECT_EQ( m.at( 1 ), 10 );
    EXPECT_EQ( m.at( 2 ), 20 );
    EXPECT_EQ( m.at( 3 ), 30 );
}

TEST( flat_map, unsorted_container_with_duplicates )
{
    std::vector<int> keys{ 3, 1, 3, 2 };
    std::vector<int> vals{ 30, 10, 99, 20 };
    FM m( std::move( keys ), std::move( vals ) );
    EXPECT_EQ( m.size(), 3 );
    EXPECT_EQ( m.at( 3 ), 30 ); // first occurrence wins after sort
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
    EXPECT_EQ( erased, 1 );
    EXPECT_EQ( m.size(), 2 );
    EXPECT_FALSE( m.contains( std::string_view{ "b" } ) );
}

TEST( flat_map, insert_hint_value )
{
    FM m{ { 1, 10 }, { 5, 50 } };
    auto it{ m.insert( m.begin() + 1, std::pair{ 3, 30 } ) };
    EXPECT_EQ( it->first, 3 );
    EXPECT_EQ( it->second, 30 );
    EXPECT_EQ( m.size(), 3 );
}

TEST( flat_map, insert_or_assign_hinted )
{
    FM m{ { 1, 10 }, { 3, 30 }, { 5, 50 } };
    // Hit: update existing
    auto it{ m.insert_or_assign( m.begin() + 1, 3, 999 ) };
    EXPECT_EQ( it->second, 999 );
    EXPECT_EQ( m.size(), 3 );
    // Miss: fallback to non-hinted
    auto it2{ m.insert_or_assign( m.begin(), 4, 40 ) };
    EXPECT_EQ( it2->second, 40 );
    EXPECT_EQ( m.size(), 4 );
}

TEST( flat_map, initializer_list_assignment )
{
    FM m{ { 1, 10 } };
    m = { { 2, 20 }, { 3, 30 } };
    EXPECT_EQ( m.size(), 2 );
    EXPECT_FALSE( m.contains( 1 ) );
    EXPECT_EQ( m.at( 2 ), 20 );
}

TEST( flat_map, deduction_guide_initializer_list )
{
    psi::vm::flat_map m{ std::pair{ 1, 2 }, std::pair{ 3, 4 } };
    static_assert( std::is_same_v<typename decltype( m )::key_type, int> );
    static_assert( std::is_same_v<typename decltype( m )::mapped_type, int> );
    EXPECT_EQ( m.size(), 2 );
}

TEST( flat_map, deduction_guide_sorted_unique_initializer_list )
{
    psi::vm::flat_map m( psi::vm::sorted_unique, { std::pair{ 1, 10 }, std::pair{ 2, 20 } } );
    EXPECT_EQ( m.size(), 2 );
    EXPECT_EQ( m.at( 1 ), 10 );
}

// Alias CTAD for container-pair and iterator-range doesn't work because Key/T
// aren't deducible from alias template params via container::value_type.
// These tests verify CTAD on flat_map directly (where the explicit
// deduction guides live).
TEST( flat_map, deduction_guide_container_pair )
{
    std::vector<int> k{ 3, 1, 2 };
    std::vector<double> v{ 3.0, 1.0, 2.0 };
    psi::vm::flat_map m( std::move( k ), std::move( v ) );
    static_assert( std::is_same_v<typename decltype( m )::key_type, int> );
    static_assert( std::is_same_v<typename decltype( m )::mapped_type, double> );
    EXPECT_EQ( m.size(), 3 );
    EXPECT_DOUBLE_EQ( m.at( 1 ), 1.0 );
}

TEST( flat_map, deduction_guide_iterator_range )
{
    std::vector<std::pair<int, double>> src{ { 1, 1.0 }, { 2, 2.0 } };
    psi::vm::flat_map m( src.begin(), src.end() );
    static_assert( std::is_same_v<typename decltype( m )::key_type, int> );
    static_assert( std::is_same_v<typename decltype( m )::mapped_type, double> );
    EXPECT_EQ( m.size(), 2 );
}

TEST( flat_map, size_type_matches_smaller_container )
{
    // With std::vector, size_type should be size_t
    static_assert( std::is_same_v<FM::size_type, std::size_t> );
}

// Test with tr_vector which has uint32_t size_type
TEST( flat_map, tr_vector_insert_range )
{
    tr_vec_map m;
    m.try_emplace( 1, 10 );
    m.try_emplace( 5, 50 );
    std::vector<std::pair<int, int>> src{ { 3, 30 }, { 2, 20 }, { 4, 40 } };
    m.insert_range( src );
    EXPECT_EQ( m.size(), 5 );
    for ( int i{ 1 }; i <= 5; ++i )
        EXPECT_EQ( m.at( i ), i * 10 );
}

TEST( flat_map, tr_vector_erase_if )
{
    tr_vec_map m;
    for ( int i{ 0 }; i < 10; ++i )
        m.try_emplace( i, i * 10 );
    auto const erased{ erase_if( m, []( auto const & kv ) { return kv.first >= 5; } ) };
    EXPECT_EQ( erased, 5 );
    EXPECT_EQ( m.size(), 5 );
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
    EXPECT_EQ( target.size(), 3 );
    EXPECT_EQ( target.at( 3 ), 30 ); // existing wins
    EXPECT_EQ( source.size(), 1 );    // duplicate stays in source
}

TEST( flat_map, tr_vector_size_type_is_uint32 )
{
    static_assert( std::is_same_v<tr_vec_map::size_type, std::uint32_t> );
}

TEST( flat_map, large_insert_range )
{
    FM m;
    std::vector<std::pair<int, int>> src;
    src.reserve( 1000 );
    for ( int i{ 999 }; i >= 0; --i )
        src.emplace_back( i, i * 10 );
    m.insert_range( src );
    EXPECT_EQ( m.size(), 1000 );
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
    EXPECT_EQ( target.size(), 500 );
    EXPECT_TRUE( source.empty() );
    EXPECT_TRUE( std::is_sorted( target.keys().begin(), target.keys().end() ) );
}

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
    // append_from with lvalue iterators: std::get<N>(forward<pair<T,T>&>(elem)) → copy
    std::vector<std::pair<tracked, tracked>> src{
        { tracked{ 3 }, tracked{ 30 } },
        { tracked{ 1 }, tracked{ 10 } },
        { tracked{ 2 }, tracked{ 20 } }
    };
    // Reset counters (vector emplacement may have copied/moved)
    for ( auto & [k, v] : src ) { k.copies = k.moves = v.copies = v.moves = 0; }

    flat_map<tracked, tracked> m;
    m.insert( src.begin(), src.end() ); // lvalue iterators → append_from

    EXPECT_EQ( m.size(), 3u );
    // Source must be intact (copied, not moved)
    for ( auto const & [k, v] : src ) {
        EXPECT_NE( k.value, -1 ) << "key was moved from";
        EXPECT_NE( v.value, -1 ) << "value was moved from";
    }
    // Each element in the map should have been copied (≥1) and never moved from source
    for ( auto it{ m.begin() }; it != m.end(); ++it ) {
        EXPECT_GT( it->first.copies, 0 ) << "key should have been copied";
        EXPECT_GT( it->second.copies, 0 ) << "value should have been copied";
    }
}

TEST( flat_map, forwarding_move_iterator_insert_moves_not_copies )
{
    // append_from with move iterators: std::get<N>(forward<pair<T,T>&&>(elem)) → move
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

TEST( flat_map, forwarding_insert_range_lvalue_copies )
{
    // insert_range from lvalue range → common view → lvalue iterators → copies
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
    // Range constructor with lvalue iterators → append_from → copies
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

TEST( flat_map, forwarding_constructor_move_iterator_moves )
{
    // Range constructor with move iterators → append_from → moves
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

TEST( flat_map, forwarding_sorted_unique_insert_lvalue_copies )
{
    // insert(sorted_unique, ...) with lvalue iterators → append_from → copies
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
    // merge(flat_map&&) → append_move_containers → moves
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
