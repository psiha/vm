////////////////////////////////////////////////////////////////////////////////
/// psi::vm flat_set / flat_multiset test suite
///
/// Typed tests exercise core flat_set behaviour across three container backends
/// (std::vector, tr_vector, std::deque). Standalone tests cover container-
/// specific features, transparent comparators, CTAD, forwarding, pass_in_reg,
/// LookupType, and flat_multiset.
////////////////////////////////////////////////////////////////////////////////

#include <psi/vm/containers/flat_set.hpp>
#include <psi/vm/containers/tr_vector.hpp>
#include <psi/vm/containers/abi.hpp>
#include <psi/vm/containers/lookup.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

//------------------------------------------------------------------------------
namespace psi::vm {
//------------------------------------------------------------------------------

//==============================================================================
// Typed-test infrastructure
//==============================================================================

template <typename KC>
struct set_config
{
    using key_container = KC;
    using key_type      = typename KC::value_type;
    using set_type      = flat_set<key_type, std::less<key_type>, KC>;
};

template <typename C>
class flat_set_typed : public ::testing::Test {};

using set_configs = ::testing::Types<
    set_config< std::vector<int>              >,
    set_config< tr_vector<int, std::uint32_t> >,
    set_config< std::deque<int>               >
>;

TYPED_TEST_SUITE( flat_set_typed, set_configs );

//==============================================================================
// Construction
//==============================================================================

TYPED_TEST( flat_set_typed, default_construction )
{
    typename TypeParam::set_type s;
    EXPECT_TRUE( s.empty() );
    EXPECT_EQ( s.size(), 0u );
    EXPECT_EQ( s.begin(), s.end() );
}

TYPED_TEST( flat_set_typed, initializer_list_construction )
{
    typename TypeParam::set_type s{ 3, 1, 4, 1, 5, 9 };
    EXPECT_EQ( s.size(), 5u ); // 1 duplicate removed
}

TYPED_TEST( flat_set_typed, range_construction )
{
    std::vector<int> src{ 5, 2, 5, 3, 1 };
    typename TypeParam::set_type s( src.begin(), src.end() );
    EXPECT_EQ( s.size(), 4u );
    EXPECT_TRUE( s.contains( 1 ) );
    EXPECT_TRUE( s.contains( 5 ) );
}

TYPED_TEST( flat_set_typed, from_range_construction )
{
    std::vector<int> src{ 4, 2, 4, 1 };
    typename TypeParam::set_type s( std::from_range, src );
    EXPECT_EQ( s.size(), 3u );
}

TYPED_TEST( flat_set_typed, copy_construction )
{
    typename TypeParam::set_type orig{ 1, 2, 3 };
    typename TypeParam::set_type copy{ orig };
    EXPECT_EQ( copy.size(), 3u );
    EXPECT_TRUE( copy.contains( 2 ) );
}

TYPED_TEST( flat_set_typed, move_construction )
{
    typename TypeParam::set_type orig{ 1, 2, 3 };
    typename TypeParam::set_type moved{ std::move( orig ) };
    EXPECT_EQ( moved.size(), 3u );
}

TYPED_TEST( flat_set_typed, sorted_unique_container_construction )
{
    typename TypeParam::key_container kc;
    kc.push_back( 1 );
    kc.push_back( 3 );
    kc.push_back( 5 );
    typename TypeParam::set_type s( sorted_unique, std::move( kc ) );
    EXPECT_EQ( s.size(), 3u );
    EXPECT_TRUE( s.contains( 1 ) );
    EXPECT_TRUE( s.contains( 3 ) );
    EXPECT_TRUE( s.contains( 5 ) );
}

TYPED_TEST( flat_set_typed, unsorted_container_construction )
{
    typename TypeParam::key_container kc;
    kc.push_back( 3 );
    kc.push_back( 1 );
    kc.push_back( 4 );
    kc.push_back( 1 );
    kc.push_back( 5 );
    typename TypeParam::set_type s( std::move( kc ) );
    EXPECT_EQ( s.size(), 4u ); // 1 duplicate removed
    auto const & k{ s.keys() };
    EXPECT_TRUE( std::is_sorted( k.begin(), k.end() ) );
}

//==============================================================================
// Lookup
//==============================================================================

TYPED_TEST( flat_set_typed, find_hit_and_miss )
{
    typename TypeParam::set_type s{ 10, 20, 30 };
    EXPECT_NE( s.find( 20 ), s.end() );
    EXPECT_EQ( *s.find( 20 ), 20 );
    EXPECT_EQ( s.find( 25 ), s.end() );
}

TYPED_TEST( flat_set_typed, contains_and_count )
{
    typename TypeParam::set_type s{ 1, 2, 3, 4 };
    EXPECT_TRUE( s.contains( 3 ) );
    EXPECT_FALSE( s.contains( 5 ) );
    EXPECT_EQ( s.count( 3 ), 1u );
    EXPECT_EQ( s.count( 5 ), 0u );
}

TYPED_TEST( flat_set_typed, lower_upper_bound )
{
    typename TypeParam::set_type s{ 10, 20, 30, 40 };
    auto lb{ s.lower_bound( 25 ) };
    auto ub{ s.upper_bound( 25 ) };
    EXPECT_EQ( *lb, 30 );
    EXPECT_EQ( *ub, 30 );
    EXPECT_EQ( lb, ub ); // no element == 25

    lb = s.lower_bound( 20 );
    ub = s.upper_bound( 20 );
    EXPECT_EQ( *lb, 20 );
    EXPECT_EQ( *ub, 30 );
}

TYPED_TEST( flat_set_typed, equal_range )
{
    typename TypeParam::set_type s{ 10, 20, 30 };
    auto [lo, hi]{ s.equal_range( 20 ) };
    EXPECT_EQ( *lo, 20 );
    EXPECT_EQ( *hi, 30 );
}

//==============================================================================
// Modifiers
//==============================================================================

TYPED_TEST( flat_set_typed, insert_single )
{
    typename TypeParam::set_type s;
    auto [it1, ok1]{ s.insert( 10 ) };
    EXPECT_TRUE( ok1 );
    EXPECT_EQ( *it1, 10 );

    auto [it2, ok2]{ s.insert( 10 ) };
    EXPECT_FALSE( ok2 ); // duplicate
    EXPECT_EQ( s.size(), 1u );
}

TYPED_TEST( flat_set_typed, emplace )
{
    typename TypeParam::set_type s;
    auto [it, ok]{ s.emplace( 42 ) };
    EXPECT_TRUE( ok );
    EXPECT_EQ( *it, 42 );

    auto [it2, ok2]{ s.emplace( 42 ) };
    EXPECT_FALSE( ok2 );
}

TYPED_TEST( flat_set_typed, emplace_hint_sorted_input )
{
    typename TypeParam::set_type s;
    auto it{ s.begin() };
    for ( int i{ 0 }; i < 10; ++i )
        it = s.emplace_hint( it, i ) + 1;
    EXPECT_EQ( s.size(), 10u );
    EXPECT_TRUE( std::is_sorted( s.keys().begin(), s.keys().end() ) );
}

TYPED_TEST( flat_set_typed, bulk_insert )
{
    typename TypeParam::set_type s{ 1, 5 };
    std::vector<int> more{ 3, 5, 7, 1 };
    s.insert( more.begin(), more.end() );
    EXPECT_EQ( s.size(), 4u ); // 1, 3, 5, 7
    EXPECT_TRUE( s.contains( 3 ) );
    EXPECT_TRUE( s.contains( 7 ) );
}

TYPED_TEST( flat_set_typed, insert_sorted_unique )
{
    typename TypeParam::set_type s{ 1, 5 };
    std::vector<int> more{ 2, 3, 4 };
    s.insert( sorted_unique, more.begin(), more.end() );
    EXPECT_EQ( s.size(), 5u );
}

TYPED_TEST( flat_set_typed, insert_initializer_list )
{
    typename TypeParam::set_type s;
    s.insert( { 3, 1, 4, 1, 5 } );
    EXPECT_EQ( s.size(), 4u );
}

TYPED_TEST( flat_set_typed, erase_by_key )
{
    typename TypeParam::set_type s{ 1, 2, 3, 4, 5 };
    EXPECT_EQ( s.erase( 3 ), 1u );
    EXPECT_EQ( s.erase( 99 ), 0u );
    EXPECT_EQ( s.size(), 4u );
    EXPECT_FALSE( s.contains( 3 ) );
}

TYPED_TEST( flat_set_typed, erase_by_iterator )
{
    typename TypeParam::set_type s{ 10, 20, 30 };
    auto it{ s.find( 20 ) };
    auto next{ s.erase( it ) };
    EXPECT_EQ( s.size(), 2u );
    EXPECT_EQ( *next, 30 );
}

TYPED_TEST( flat_set_typed, erase_range )
{
    typename TypeParam::set_type s{ 1, 2, 3, 4, 5 };
    auto first{ s.find( 2 ) };
    auto last { s.find( 4 ) };
    s.erase( first, last ); // erases 2, 3
    EXPECT_EQ( s.size(), 3u );
    EXPECT_TRUE( s.contains( 1 ) );
    EXPECT_TRUE( s.contains( 4 ) );
    EXPECT_TRUE( s.contains( 5 ) );
}

TYPED_TEST( flat_set_typed, clear )
{
    typename TypeParam::set_type s{ 1, 2, 3 };
    s.clear();
    EXPECT_TRUE( s.empty() );
}

TYPED_TEST( flat_set_typed, swap )
{
    typename TypeParam::set_type a{ 1, 2 };
    typename TypeParam::set_type b{ 3, 4, 5 };
    a.swap( b );
    EXPECT_EQ( a.size(), 3u );
    EXPECT_EQ( b.size(), 2u );
    EXPECT_TRUE( a.contains( 3 ) );
    EXPECT_TRUE( b.contains( 1 ) );
}

//==============================================================================
// Extract / Replace
//==============================================================================

TYPED_TEST( flat_set_typed, extract_and_replace )
{
    typename TypeParam::set_type s{ 1, 2, 3 };
    auto keys{ s.extract() };
    EXPECT_EQ( keys.size(), 3u );
    EXPECT_TRUE( s.empty() );

    keys.push_back( 4 );
    s.replace( std::move( keys ) );
    EXPECT_EQ( s.size(), 4u );
}

//==============================================================================
// Merge
//==============================================================================

TYPED_TEST( flat_set_typed, merge_non_overlapping )
{
    typename TypeParam::set_type a{ 1, 3, 5 };
    typename TypeParam::set_type b{ 2, 4, 6 };
    a.merge( b );
    EXPECT_EQ( a.size(), 6u );
    EXPECT_TRUE( b.empty() );
}

TYPED_TEST( flat_set_typed, merge_overlapping )
{
    typename TypeParam::set_type a{ 1, 2, 3 };
    typename TypeParam::set_type b{ 2, 3, 4, 5 };
    a.merge( b );
    EXPECT_EQ( a.size(), 5u ); // 1..5
    EXPECT_EQ( b.size(), 2u ); // 2, 3 stayed (already in a)
}

TYPED_TEST( flat_set_typed, merge_self )
{
    typename TypeParam::set_type s{ 1, 2, 3 };
    s.merge( s );
    EXPECT_EQ( s.size(), 3u ); // no change
}

TYPED_TEST( flat_set_typed, merge_rvalue )
{
    typename TypeParam::set_type a{ 1, 3 };
    typename TypeParam::set_type b{ 2, 4 };
    a.merge( std::move( b ) );
    EXPECT_EQ( a.size(), 4u );
}

//==============================================================================
// Comparison / erase_if
//==============================================================================

TYPED_TEST( flat_set_typed, comparison )
{
    typename TypeParam::set_type a{ 1, 2, 3 };
    typename TypeParam::set_type b{ 1, 2, 3 };
    typename TypeParam::set_type c{ 1, 2, 4 };
    EXPECT_EQ( a, b );
    EXPECT_NE( a, c );
    EXPECT_LT( a, c );
}

TYPED_TEST( flat_set_typed, erase_if_basic )
{
    typename TypeParam::set_type s{ 1, 2, 3, 4, 5, 6 };
    auto erased{ erase_if( s, []( int x ) { return x % 2 == 0; } ) };
    EXPECT_EQ( erased, 3u );
    EXPECT_EQ( s.size(), 3u ); // 1, 3, 5
    EXPECT_FALSE( s.contains( 2 ) );
}

//==============================================================================
// Edge cases
//==============================================================================

TYPED_TEST( flat_set_typed, empty_operations )
{
    typename TypeParam::set_type s;
    EXPECT_EQ( s.find( 1 ), s.end() );
    EXPECT_FALSE( s.contains( 1 ) );
    EXPECT_EQ( s.count( 1 ), 0u );
    EXPECT_EQ( s.lower_bound( 1 ), s.end() );
    EXPECT_EQ( s.erase( 1 ), 0u );
}

//==============================================================================
// Iterator
//==============================================================================

TYPED_TEST( flat_set_typed, iterator_random_access )
{
    typename TypeParam::set_type s{ 10, 20, 30, 40 };
    auto it{ s.begin() };
    EXPECT_EQ( *it, 10 );
    EXPECT_EQ( it[ 2 ], 30 );
    it += 3;
    EXPECT_EQ( *it, 40 );
    it -= 2;
    EXPECT_EQ( *it, 20 );
    EXPECT_EQ( s.end() - s.begin(), 4 );
}

TYPED_TEST( flat_set_typed, reverse_iterator )
{
    typename TypeParam::set_type s{ 1, 2, 3 };
    std::vector<int> rev;
    for ( auto it{ s.rbegin() }; it != s.rend(); ++it )
        rev.push_back( *it );
    EXPECT_EQ( rev, ( std::vector<int>{ 3, 2, 1 } ) );
}

//==============================================================================
// Misc
//==============================================================================

TYPED_TEST( flat_set_typed, initializer_list_assignment )
{
    typename TypeParam::set_type s{ 1, 2, 3 };
    s = { 10, 20 };
    EXPECT_EQ( s.size(), 2u );
    EXPECT_TRUE( s.contains( 10 ) );
    EXPECT_FALSE( s.contains( 1 ) );
}

TYPED_TEST( flat_set_typed, insert_range_basic )
{
    typename TypeParam::set_type s{ 1, 3 };
    std::vector<int> rg{ 2, 4, 5 };
    s.insert_range( rg );
    EXPECT_EQ( s.size(), 5u );
}

TYPED_TEST( flat_set_typed, insert_range_sorted )
{
    typename TypeParam::set_type s{ 1, 5 };
    std::vector<int> rg{ 2, 3, 4 };
    s.insert_range( sorted_unique, rg );
    EXPECT_EQ( s.size(), 5u );
}

TYPED_TEST( flat_set_typed, keys_returns_sorted )
{
    typename TypeParam::set_type s{ 5, 3, 1, 4, 2 };
    auto const & k{ s.keys() };
    EXPECT_EQ( k.size(), 5u );
    EXPECT_TRUE( std::is_sorted( k.begin(), k.end() ) );
}

//==============================================================================
// Standalone type aliases
//==============================================================================

using FS  = flat_set<int>;
using FSS = flat_set<std::string>;

// tr_vector-backed set with uint32_t size_type
using tr_vec_set = flat_set<int, std::less<int>, tr_vector<int, std::uint32_t>>;

// Transparent comparator set
struct TransComp {
    using is_transparent = void;
    bool operator()( int a, int b )                const noexcept { return a < b; }
    bool operator()( int a, long b )               const noexcept { return a < b; }
    bool operator()( long a, int b )               const noexcept { return a < b; }
    bool operator()( std::string_view a, std::string_view b ) const noexcept { return a < b; }
};
using TransSet = flat_set<int, TransComp>;

// Multiset alias
using FMS = flat_multiset<int>;

//==============================================================================
// Container-specific tests (vector only: .data(), .reserve(), .capacity(), span)
//==============================================================================

TEST( flat_set, reserve_and_shrink )
{
    FS s;
    s.reserve( 100 );
    EXPECT_GE( s.capacity(), 100u );
    s.insert( { 1, 2, 3 } );
    s.shrink_to_fit();
    EXPECT_EQ( s.capacity(), 3u );
}

TEST( flat_set, span_conversion )
{
    FS s{ 10, 20, 30 };
    std::span<int const> sp{ s };
    EXPECT_EQ( sp.size(), 3u );
    EXPECT_EQ( sp[ 0 ], 10 );
    EXPECT_EQ( sp[ 2 ], 30 );
}

TEST( flat_set, sequence_alias )
{
    FS s{ 1, 2, 3 };
    EXPECT_EQ( &s.keys(), &s.sequence() );
}

TEST( flat_set, tr_vector_size_type )
{
    static_assert( std::is_same_v<tr_vec_set::size_type, std::uint32_t> );
}

TEST( flat_set, tr_vector_reserve_capacity )
{
    tr_vec_set s;
    s.reserve( 50 );
    EXPECT_GE( s.capacity(), 50u );
    s.insert( { 1, 2, 3 } );
    auto const capBefore{ s.capacity() };
    s.shrink_to_fit();
    EXPECT_LE( s.capacity(), capBefore );
    EXPECT_GE( s.capacity(), s.size() );
}

//==============================================================================
// Transparent comparator
//==============================================================================

TEST( flat_set, transparent_comparison )
{
    TransSet s{ 1, 2, 3 };
    EXPECT_NE( s.find( 2L ), s.end() );   // long -> int transparent
    EXPECT_TRUE( s.contains( 3L ) );
    EXPECT_EQ( s.count( 1L ), 1u );
}

TEST( flat_set, transparent_erase )
{
    TransSet s{ 1, 2, 3, 4 };
    EXPECT_EQ( s.erase( 2L ), 1u );
    EXPECT_EQ( s.size(), 3u );
    EXPECT_FALSE( s.contains( 2 ) );
}

//==============================================================================
// CTAD (deduction guides)
//==============================================================================

TEST( flat_set, deduction_guide_initializer_list )
{
    flat_set s{ 3, 1, 4, 1, 5 };
    static_assert( std::is_same_v<typename decltype( s )::key_type, int> );
    EXPECT_EQ( s.size(), 4u );
}

TEST( flat_set, deduction_guide_sorted_unique_initializer_list )
{
    flat_set s( sorted_unique, { 1, 2, 3 } );
    EXPECT_EQ( s.size(), 3u );
}

TEST( flat_set, deduction_guide_container )
{
    std::vector<int> v{ 3, 1, 2 };
    flat_set s( std::move( v ) );
    static_assert( std::is_same_v<typename decltype( s )::key_type, int> );
    EXPECT_EQ( s.size(), 3u );
}

TEST( flat_set, deduction_guide_iterator_range )
{
    std::vector<int> src{ 5, 3, 1, 3 };
    flat_set s( src.begin(), src.end() );
    static_assert( std::is_same_v<typename decltype( s )::key_type, int> );
    EXPECT_EQ( s.size(), 3u );
}

//==============================================================================
// key_comp_mutable
//==============================================================================

TEST( flat_set, key_comp_mutable_accessible )
{
    TransSet s{ 1, 2, 3 };
    auto & comp{ s.key_comp_mutable() };
    EXPECT_TRUE( comp( 1, 2 ) );
    (void)comp;
}

//==============================================================================
// Forwarding correctness
//==============================================================================

TEST( flat_set, forwarding_lvalue_copies )
{
    std::vector<std::string> src{ "hello", "world" };
    flat_set<std::string> s( src.begin(), src.end() );
    EXPECT_EQ( s.size(), 2u );
    // Source should still have values (copied, not moved)
    EXPECT_FALSE( src[ 0 ].empty() );
    EXPECT_FALSE( src[ 1 ].empty() );
}

TEST( flat_set, forwarding_move_iterator_moves )
{
    std::vector<std::string> src{ "alpha", "beta", "gamma" };
    flat_set<std::string> s( std::make_move_iterator( src.begin() ), std::make_move_iterator( src.end() ) );
    EXPECT_EQ( s.size(), 3u );
    // Source strings should be moved-from
    for ( auto const & str : src )
        EXPECT_TRUE( str.empty() );
}

//==============================================================================
// pass_in_reg — static assertions on stored_type
//==============================================================================

TEST( flat_set, pass_in_reg_trivial_by_value )
{
    // Trivial small types should be stored by value (not by reference)
    using PIR_int = pass_in_reg<int>;
    static_assert( PIR_int::pass_by_val );
    static_assert( std::is_same_v<PIR_int::stored_type, int> );

    using PIR_u32 = pass_in_reg<std::uint32_t>;
    static_assert( PIR_u32::pass_by_val );
    static_assert( std::is_same_v<PIR_u32::stored_type, std::uint32_t> );

    using PIR_ptr = pass_in_reg<void const *>;
    static_assert( PIR_ptr::pass_by_val );
    static_assert( std::is_same_v<PIR_ptr::stored_type, void const *> );
}

TEST( flat_set, pass_in_reg_string_becomes_string_view )
{
    // std::string is non-trivial — pass_in_reg should convert to string_view
    using PIR_str = pass_in_reg<std::string>;
    static_assert( !PIR_str::pass_by_val );
    static_assert( std::is_same_v<PIR_str::stored_type, std::string_view> );
}

TEST( flat_set, pass_in_reg_preserves_heterogeneous_type )
{
    // When used with a different type (e.g. char const * for a string set),
    // CTAD should produce pass_in_reg<char const *> with by-value storage
    char const * cstr{ "hello" };
    auto pir{ pass_in_reg{ cstr } };
    static_assert( std::is_same_v<decltype( pir )::value_type, char const * > );
    static_assert( std::is_same_v<decltype( pir )::stored_type, char const * > );
    static_assert( decltype( pir )::pass_by_val );
}

//==============================================================================
// LookupType concept — acceptance / rejection
//==============================================================================

TEST( flat_set, lookup_type_concept_same_type_always_accepted )
{
    // key_type itself is always accepted regardless of transparency
    static_assert(  LookupType<int, true,  int> );
    static_assert(  LookupType<int, false, int> );
    static_assert(  LookupType<std::string, true,  std::string> );
    static_assert(  LookupType<std::string, false, std::string> );
}

TEST( flat_set, lookup_type_concept_transparent_accepts_any )
{
    // With transparent comparator, any type is accepted
    static_assert(  LookupType<long,         true, int> );
    static_assert(  LookupType<char const *, true, std::string> );
    static_assert(  LookupType<std::string_view, true, std::string> );
}

TEST( flat_set, lookup_type_concept_non_transparent_requires_convertible )
{
    // Without transparency, type must be convertible to key_type
    static_assert(  LookupType<char const *, false, std::string> ); // char const * -> string
    static_assert(  LookupType<long,         false, int> );         // long -> int

    // A type not convertible to key_type should be rejected
    struct Unconvertible {};
    static_assert( !LookupType<Unconvertible, false, int> );
    static_assert( !LookupType<Unconvertible, false, std::string> );
}

//==============================================================================
// Transparent lookup — verify zero unnecessary conversions
//==============================================================================

namespace
{
    // A string-like key type that counts how many times std::string is
    // constructed from it. Used to verify that transparent lookups do NOT
    // trigger per-comparison string constructions.
    inline int g_string_ctor_from_sv_count{ 0 };

    struct CountingString : std::string
    {
        using std::string::string;
        using std::string::operator=;

        // Track constructions from string_view (proxy for char const *)
        CountingString( std::string_view sv ) : std::string{ sv } { ++g_string_ctor_from_sv_count; }
    };

    struct CountingStringLess
    {
        using is_transparent = void;
        bool operator()( std::string_view a, std::string_view b ) const noexcept { return a < b; }
    };
} // anon

TEST( flat_set, transparent_lookup_zero_string_constructions )
{
    // Build a transparent set of CountingString
    flat_set<CountingString, CountingStringLess> s;
    s.insert( CountingString{ "alpha" } );
    s.insert( CountingString{ "beta"  } );
    s.insert( CountingString{ "gamma" } );
    s.insert( CountingString{ "delta" } );

    g_string_ctor_from_sv_count = 0;

    // Lookup with char const * — transparent comparator handles string_view
    // directly, so ZERO CountingString constructions should occur.
    char const * key{ "beta" };
    auto it{ s.find( key ) };
    EXPECT_NE( it, s.end() );
    EXPECT_EQ( *it, "beta" );
    EXPECT_EQ( g_string_ctor_from_sv_count, 0 ) << "find() with transparent comparator should not construct strings";

    // contains() — also zero constructions
    EXPECT_TRUE( s.contains( key ) );
    EXPECT_EQ( g_string_ctor_from_sv_count, 0 ) << "contains() with transparent comparator should not construct strings";

    // count() — also zero constructions
    EXPECT_EQ( s.count( key ), 1u );
    EXPECT_EQ( g_string_ctor_from_sv_count, 0 ) << "count() with transparent comparator should not construct strings";

    // lower_bound / upper_bound — also zero constructions
    auto lb{ s.lower_bound( key ) };
    auto ub{ s.upper_bound( key ) };
    EXPECT_EQ( g_string_ctor_from_sv_count, 0 ) << "lower/upper_bound with transparent comparator should not construct strings";
    EXPECT_EQ( std::distance( lb, ub ), 1 );

    // equal_range — also zero constructions
    auto [er_lb, er_ub]{ s.equal_range( key ) };
    EXPECT_EQ( g_string_ctor_from_sv_count, 0 ) << "equal_range with transparent comparator should not construct strings";
    EXPECT_EQ( std::distance( er_lb, er_ub ), 1 );
}

TEST( flat_set, exact_key_type_lookup_zero_conversions )
{
    flat_set<CountingString, CountingStringLess> s;
    s.insert( CountingString{ "alpha" } );
    s.insert( CountingString{ "beta"  } );
    s.insert( CountingString{ "gamma" } );

    g_string_ctor_from_sv_count = 0;

    // Lookup with exact key_type — no conversion should happen
    CountingString const key{ "beta" };
    g_string_ctor_from_sv_count = 0; // reset after key construction

    auto it{ s.find( key ) };
    EXPECT_NE( it, s.end() );
    EXPECT_EQ( g_string_ctor_from_sv_count, 0 ) << "find() with exact key_type should not construct additional strings";
}

TEST( flat_set, transparent_string_set_lookup_with_string_view )
{
    // Standard transparent string set with std::less<>
    flat_set<std::string, std::less<>> s{ "alpha", "beta", "gamma", "delta" };

    std::string_view sv{ "beta" };
    EXPECT_TRUE( s.contains( sv ) );
    EXPECT_NE( s.find( sv ), s.end() );
    EXPECT_EQ( s.count( sv ), 1u );
    EXPECT_EQ( *s.find( sv ), "beta" );

    // Also with char const *
    EXPECT_TRUE( s.contains( "gamma" ) );
    EXPECT_NE( s.find( "gamma" ), s.end() );
}

TEST( flat_set, transparent_int_set_lookup_with_long )
{
    flat_set<int, std::less<>> s{ 10, 20, 30, 40, 50 };

    // Lookup with long — heterogeneous, no conversion needed (transparent)
    EXPECT_TRUE( s.contains( 30L ) );
    EXPECT_EQ( s.count( 30L ), 1u );
    EXPECT_NE( s.find( 30L ), s.end() );
    EXPECT_EQ( *s.find( 30L ), 30 );

    // Lookup with unsigned
    EXPECT_TRUE( s.contains( 40u ) );
}

//==============================================================================
// flat_multiset — Basic operations
//==============================================================================

TEST( flat_multiset, allows_duplicates )
{
    FMS s{ 3, 1, 4, 1, 5, 1 };
    EXPECT_EQ( s.size(), 6u );
    EXPECT_EQ( s.count( 1 ), 3u );
}

TEST( flat_multiset, insert_returns_iterator )
{
    FMS s;
    auto it{ s.insert( 10 ) };
    EXPECT_EQ( *it, 10 );
    auto it2{ s.insert( 10 ) };
    EXPECT_EQ( *it2, 10 );
    EXPECT_EQ( s.size(), 2u );
}

TEST( flat_multiset, emplace )
{
    FMS s;
    auto it{ s.emplace( 42 ) };
    EXPECT_EQ( *it, 42 );
    auto it2{ s.emplace( 42 ) };
    EXPECT_EQ( *it2, 42 );
    EXPECT_EQ( s.size(), 2u );
}

TEST( flat_multiset, erase_all_matching )
{
    FMS s{ 1, 2, 2, 2, 3 };
    EXPECT_EQ( s.erase( 2 ), 3u );
    EXPECT_EQ( s.size(), 2u );
    EXPECT_EQ( s.count( 2 ), 0u );
}

TEST( flat_multiset, equal_range )
{
    FMS s{ 1, 2, 2, 2, 3, 4 };
    auto [lo, hi]{ s.equal_range( 2 ) };
    EXPECT_EQ( hi - lo, 3 );
    for ( auto it{ lo }; it != hi; ++it )
        EXPECT_EQ( *it, 2 );
}

TEST( flat_multiset, sorted_equivalent_construction )
{
    std::vector<int> v{ 1, 2, 2, 3, 3, 3 };
    FMS s( sorted_equivalent, std::move( v ) );
    EXPECT_EQ( s.size(), 6u );
    EXPECT_EQ( s.count( 3 ), 3u );
}

TEST( flat_multiset, merge_transfers_all )
{
    FMS a{ 1, 2 };
    FMS b{ 2, 3 };
    a.merge( b );
    EXPECT_EQ( a.size(), 4u ); // 1, 2, 2, 3 — no dedup
    EXPECT_EQ( a.count( 2 ), 2u );
    EXPECT_TRUE( b.empty() );
}

TEST( flat_multiset, bulk_insert_keeps_duplicates )
{
    FMS s{ 1, 2 };
    std::vector<int> more{ 2, 3, 3 };
    s.insert( more.begin(), more.end() );
    EXPECT_EQ( s.size(), 5u ); // 1, 2, 2, 3, 3
}

TEST( flat_multiset, comparison )
{
    FMS a{ 1, 2, 2 };
    FMS b{ 1, 2, 2 };
    FMS c{ 1, 2, 3 };
    EXPECT_EQ( a, b );
    EXPECT_NE( a, c );
    EXPECT_LT( a, c );
}

TEST( flat_multiset, erase_if )
{
    FMS s{ 1, 2, 2, 3, 3, 3 };
    auto erased{ erase_if( s, []( int x ) { return x == 2; } ) };
    EXPECT_EQ( erased, 2u );
    EXPECT_EQ( s.size(), 4u );
}

//==============================================================================
// flat_multiset — Deduction guides
//==============================================================================

TEST( flat_multiset, deduction_guide_sorted_equivalent )
{
    flat_multiset s( sorted_equivalent, std::vector<int>{ 1, 2, 2, 3 } );
    EXPECT_EQ( s.size(), 4u );
    EXPECT_EQ( s.count( 2 ), 2u );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
