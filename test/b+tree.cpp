#include <psi/vm/containers/b+tree.hpp>
#include <psi/vm/containers/b+tree_print.hpp>

#include <boost/assert.hpp>
#include <boost/container/flat_set.hpp>

#define HAVE_ABSL 0
#if HAVE_ABSL
#include <absl/container/btree_set.h>
#endif

#include <gtest/gtest.h>

#include <chrono>
#include <print>
#include <random>
#include <ranges>
#include <utility>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

#ifdef NDEBUG // bench only release builds

namespace
{
    using timer    = std::chrono::high_resolution_clock;
    using duration = std::chrono::nanoseconds;

    duration time_insertion( auto & container, auto const & data )
    {
        auto const start{ timer::now() };
        container.insert( data.begin(), data.end() );
        return std::chrono::duration_cast<duration>( timer::now() - start ) / data.size();
    }
    duration time_lookup( auto const & container, auto const & data ) noexcept
    {
        auto const start{ timer::now() };
        for ( auto const x : data ) {
            EXPECT_EQ( *container.find( x ), x );
        }
        return std::chrono::duration_cast<duration>( timer::now() - start ) / data.size();
    }
} // anonymous namespace

TEST( bp_tree, benchamrk )
{
    auto const   test_size{ 7654321 };
    auto const   seed{ std::random_device{}() };
    std::mt19937 rng{ seed };

    std::ranges::iota_view constexpr sorted_numbers{ 0, test_size };
    auto numbers{ std::ranges::to<std::vector>( sorted_numbers ) };
    std::ranges::shuffle( numbers, rng );

    psi::vm         ::bptree_set<int> bpt; bpt.map_memory();
    boost::container::flat_set  <int> flat_set;
#if HAVE_ABSL
    absl            ::btree_set <int> abpt;
#endif

    // bulk-insertion-into-empty
    auto const flat_set_insert{ time_insertion( flat_set, sorted_numbers ) };
    auto const bpt_insert     { time_insertion( bpt     , sorted_numbers ) };
#if HAVE_ABSL
    auto const abpt_insert    { time_insertion( abpt    , sorted_numbers ) };
#endif

    // random lookup
    auto const flat_set_find{ time_lookup( flat_set, numbers ) };
    auto const      bpt_find{ time_lookup( bpt     , numbers ) };
#if HAVE_ABSL
    auto const     abpt_find{ time_lookup( abpt    , numbers ) };
#endif

    std::println( "insert / lookup:" );
    std::println( "\t boost::container::flat_set:\t{} / {}", flat_set_insert, flat_set_find );
    std::println( "\t psi::vm::bpt:\t{} / {}", bpt_insert, bpt_find );
#if HAVE_ABSL
    std::println( "\t absl::bpt:\t{} / {}", abpt_insert, abpt_find );
#endif

#if 0 // CI servers are unreliable for comparative perf tests
    EXPECT_LE( bpt_find, flat_set_find );
#endif
} // bp_tree.benchamrk
#endif // release build

static auto const test_file{ "test.bpt" };

TEST( bp_tree, playground )
{
    // TODO different types, insertion from non contiguous containers

#ifdef NDEBUG
    auto const test_size{ 4853735 };
#else
    auto const test_size{  258735 };
#endif
    std::ranges::iota_view constexpr sorted_numbers{ 0, test_size };
    auto const seed{ std::random_device{}() };
    std::println( "Seed {}", seed );
    std::mt19937 rng{ seed };
    auto numbers{ std::ranges::to<std::vector>( sorted_numbers ) };
    std::span const nums{ numbers };
    // leave the largest quarter of values at the end to trigger/exercise the
    // bulk_append branch in insert()
    std::ranges::shuffle( nums.subspan( 0, 3 * nums.size() / 4 ), rng );
    std::ranges::shuffle( nums.subspan(    3 * nums.size() / 4 ), rng );
    {
        bptree_set<int> bpt;
        bpt.map_memory( nums.size() );
        {
            auto const third{ nums.size() / 3 };
            auto const  first_third{ nums.subspan( 0 * third, third ) };
            auto const second_third{ nums.subspan( 1 * third, third ) };
            auto const  third_third{ nums.subspan( 2 * third        ) };
                                                    EXPECT_EQ( bpt.insert( first_third )       , first_third.size() );   // bulk into empty
            for ( auto const & n : second_third ) { EXPECT_EQ( bpt.insert( n           ).second, true               ); } // single value
                                                    EXPECT_EQ( bpt.insert( third_third )       , third_third.size() );   // bulk into non-empty

            EXPECT_EQ( bpt.insert( second_third ), 0 );
        }

        static_assert( std::forward_iterator<bptree_set<int>::const_iterator> );

        EXPECT_EQ  ( std::distance( bpt.   begin(), bpt.   end() ), bpt.size() );
        EXPECT_EQ  ( std::distance( bpt.ra_begin(), bpt.ra_end() ), bpt.size() );
        EXPECT_TRUE( std::ranges::is_sorted( std::as_const( bpt ), bpt.comp() ) );
        EXPECT_TRUE( std::ranges::equal( sorted_numbers, bpt                 ) );
        EXPECT_TRUE( std::ranges::equal( sorted_numbers, bpt.random_access() ) );
        EXPECT_NE( bpt.find( +42 ), bpt.end() );
        EXPECT_EQ( bpt.find( -42 ), bpt.end() );
        EXPECT_TRUE ( bpt.erase( 42 ) );
        EXPECT_FALSE( bpt.erase( 42 ) );
        EXPECT_EQ   ( bpt.find( +42 ), bpt.end() );
        EXPECT_TRUE ( bpt.insert( 42 ).second );
        EXPECT_FALSE( bpt.insert( 42 ).second );
        EXPECT_EQ   ( *bpt.insert( 42 ).first, 42 );
        EXPECT_TRUE( std::ranges::equal(                      bpt.random_access()  ,                      sorted_numbers   ) );
        EXPECT_TRUE( std::ranges::equal( std::views::reverse( bpt.random_access() ), std::views::reverse( sorted_numbers ) ) );

        EXPECT_TRUE( bpt.erase( 42 ) );
        auto const hint42{ bpt.lower_bound( 42 ) };
        EXPECT_EQ( *hint42, 42 + 1 );
        EXPECT_EQ( *bpt.insert( hint42, 42 ), 42 );

        EXPECT_EQ( *bpt.erase( bpt.find( 42 ) ), 43 );
        EXPECT_TRUE( bpt.insert( 42 ).second );

        {
            auto const ra{ bpt.random_access() };
            for ( auto n : std::views::iota( 0, test_size / 55 ) ) // slow operation (not really amortized constant time): use a smaller subset of the input
                EXPECT_EQ( ra[ n ], n );
        }

        // merge test: to exercise the bulk_append path leave/add extra entries
        // at the end of input data, bigger than all existing values, that are
        // separately shuffled (so that they remain at the end and thus trigger
        // the bulk_append branch in merge()).
        auto const                   extra_entries_for_tree_merge{ test_size / 5 };
        std::ranges::iota_view const merge_appendix{ test_size, test_size + extra_entries_for_tree_merge };
        {
            auto const evenize{ std::views::transform( []( int const v ) { return v * 2; } ) };
            
            auto const even_sorted_numbers{ std::ranges::iota_view{ 0, test_size / 2 } | evenize };
            auto shuffled_even_numbers{ std::ranges::to<std::vector>( even_sorted_numbers ) };
            std::shuffle( shuffled_even_numbers.begin(), shuffled_even_numbers.end(), rng );
            for ( auto const & n : shuffled_even_numbers ) {
                EXPECT_TRUE( bpt.erase( n ) );
            }

            bptree_set<int> bpt_even;
            bpt_even.map_memory();
            shuffled_even_numbers.append_range( merge_appendix );
            bpt_even.insert( shuffled_even_numbers );

            EXPECT_EQ( bpt.merge( std::move( bpt_even ) ), bpt_even.size() );
        }

        EXPECT_TRUE( std::ranges::equal( std::ranges::iota_view{ 0, test_size + extra_entries_for_tree_merge }, bpt ) );

        std::shuffle( numbers.begin(), numbers.end(), rng );
        for ( auto const & n : numbers )
            EXPECT_TRUE( bpt.erase( n ) );
        // iterator erase test
        for ( auto const & n : merge_appendix ) {
            auto const next_it{ bpt.erase( bpt.find( n ) ) };
            EXPECT_TRUE( ( next_it == bpt.end() ) || ( *next_it == n + 1 ) );
        }

        EXPECT_TRUE( bpt.empty() );
    }

    {
        bptree_set<int> bpt;
        bpt.map_file( test_file, flags::named_object_construction_policy::create_new_or_truncate_existing );

        for ( auto const & n : numbers )
            EXPECT_TRUE( bpt.insert( n ).second );
    
        EXPECT_TRUE( std::ranges::is_sorted( std::as_const( bpt ), bpt.comp() ) );
        EXPECT_TRUE( std::ranges::equal( bpt, sorted_numbers ) );
        EXPECT_NE  ( bpt.find( +42 ), bpt.end() );
        EXPECT_EQ  ( bpt.find( -42 ), bpt.end() );
        EXPECT_TRUE( bpt.erase( 42 ) );
        EXPECT_EQ  ( bpt.find( +42 ), bpt.end() );
    }
    {
        bptree_set<int> bpt;
        bpt.map_file( test_file, flags::named_object_construction_policy::open_existing );

        EXPECT_EQ  ( bpt.size(), sorted_numbers.size() - 1 );
        EXPECT_TRUE( bpt.insert( +42 ).second );
    
        EXPECT_TRUE( std::ranges::is_sorted( bpt, bpt.comp() ) );
        EXPECT_TRUE( std::ranges::equal( std::as_const( bpt ), sorted_numbers ) );
        EXPECT_NE( bpt.find( +42 ), bpt.end() );
        EXPECT_EQ( bpt.find( -42 ), bpt.end() );

        bpt.clear();
#   ifndef __APPLE__ // linker error (the workaround at EOF does not help)
        bpt.print();
#   endif
    }
}

TEST( bp_tree, nonunique )
{
    auto const test_num{ 33 };
#ifdef NDEBUG
    auto const test_size{ 853735 };
#else
    auto const test_size{  23567 };
#endif
    bptree_multiset<int> bpt;
    bpt.map_memory( test_size );

    std::ranges::iota_view constexpr sorted_numbers{ 0, test_size };
    auto const seed{ std ::random_device{}() };
    std::println( "Seed {}", seed );
    std::mt19937 rng{ seed };
    auto numbers{ std::ranges::to<std::vector>( sorted_numbers ) };
    std::ranges::shuffle( numbers, rng );

    for ( auto const n : numbers )
    {
        EXPECT_EQ( *bpt.insert( n        ), n        );
        EXPECT_EQ( *bpt.insert( test_num ), test_num );
    }
    EXPECT_EQ( bpt.size(), numbers.size() * 2 );

    auto eq_range_nums{ std::ranges::to<std::vector<int>>( bpt.equal_range( test_num ) ) };
    EXPECT_EQ( eq_range_nums.size(), numbers.size() + 1 );
    std::erase( eq_range_nums, test_num );
    EXPECT_TRUE( eq_range_nums.empty() );

    std::ranges::shuffle( numbers, rng );
    EXPECT_EQ( bpt.erase( test_num ), numbers.size() + 1 );
    for ( auto const n : numbers )
        EXPECT_EQ( bpt.erase( n ), n != test_num );
    EXPECT_TRUE( bpt.empty() );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
#ifdef __APPLE__ // Xcode 16.1 Symbol not found: __ZNSt3__119__is_posix_terminalEP7__sFILE
namespace std { inline namespace __1 {
#include <unistd.h>
[[ gnu::weak, gnu::visibility( "default" ) ]]
extern bool __is_posix_terminal(FILE* __stream) { return isatty(fileno(__stream)); }
}}
#endif