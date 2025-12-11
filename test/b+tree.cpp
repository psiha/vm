#include <psi/vm/containers/b+tree.hpp>
#include <psi/vm/containers/b+tree_print.hpp>
#include <psi/vm/containers/tr_vector.hpp>

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
    auto const test_size{ 7853735 };
#else
    auto const test_size{ 1458357 };
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

            { // exercise non-unique insertions
                auto nonunique_copies{ std::ranges::to<std::vector>( second_third ) };
                EXPECT_EQ( bpt.insert( nonunique_copies ), 0 );
                auto const non_yet_inserted_value{ nums.back() * 2 };
                nonunique_copies[ nonunique_copies.size() / 2 ] = non_yet_inserted_value;
                EXPECT_EQ( bpt.insert( nonunique_copies ), 1 );
                EXPECT_TRUE( bpt.erase( non_yet_inserted_value ) );
            }
        }

        static_assert( std::forward_iterator<bptree_set<int>::const_iterator> );

        EXPECT_EQ  ( std::distance( bpt.   begin(), bpt.   end() ), bpt.size() );
        EXPECT_EQ  ( std::distance( bpt.ra_begin(), bpt.ra_end() ), bpt.size() );
#   if !__SANITIZE_ADDRESS__ // :wat:
        EXPECT_TRUE( std::ranges::is_sorted( std::as_const( bpt ), bpt.comp() ) );
#   endif
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
            for ( auto n : std::views::iota( 0, test_size / 555 ) ) // slow operation (not really amortized constant time): use a smaller subset of the input
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

            EXPECT_EQ( bpt.merge( std::move( bpt_even ) ), shuffled_even_numbers.size() );
            EXPECT_FALSE( bpt_even./*empty()*/has_attached_storage() /*TODO rethink the guarantee here*/ );
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
#   if !__SANITIZE_ADDRESS__ // :wat:
        EXPECT_TRUE( std::ranges::is_sorted( std::as_const( bpt ), bpt.comp() ) );
#   endif
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
    
#   if !__SANITIZE_ADDRESS__ // :wat:
        EXPECT_TRUE( std::ranges::is_sorted( bpt, bpt.comp() ) );
#   endif
        EXPECT_TRUE( std::ranges::equal( std::as_const( bpt ), sorted_numbers ) );
        EXPECT_NE( bpt.find( +42 ), bpt.end() );
        EXPECT_EQ( bpt.find( -42 ), bpt.end() );

        bpt.clear();
        bpt.print();
    }
}

TEST( bp_tree, nonunique )
{
    auto const test_num{ 33 };
#ifdef NDEBUG
    auto const test_size{ 1853735 };
#else
    auto const test_size{  193567 };
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

TEST( bp_tree, insert_presorted )
{
#ifdef NDEBUG
    auto const test_size{ 74853736 };
#else
    auto const test_size{  1458358 };
#endif
    std::ranges::iota_view const sorted_numbers{ 0, test_size };
    auto const seed{ std::random_device{}() };
    std::println( "Seed {}", seed );

    // Test 1: insert_presorted into empty tree
    {
        bptree_set<int> bpt;
        bpt.map_memory( test_size );
        
        auto sorted_vec{ std::ranges::to<std::vector>( sorted_numbers ) };
        EXPECT_EQ( bpt.insert_presorted( sorted_vec ), static_cast<std::size_t>( test_size ) );
        EXPECT_EQ( bpt.size(), static_cast<std::size_t>( test_size ) );
        EXPECT_TRUE( std::ranges::equal( bpt, sorted_numbers ) );
    }

    // Test 2: insert_presorted into non-empty tree (append scenario)
    {
        bptree_set<int> bpt;
        bpt.map_memory( test_size * 2 );

        auto const half{ test_size / 2 };
        std::ranges::iota_view const first_half { 0, half };
        std::ranges::iota_view const second_half{ half, test_size };
        
        auto first_half_vec{ std::ranges::to<std::vector>( first_half ) };
        EXPECT_EQ( bpt.insert_presorted( first_half_vec ), first_half_vec.size() );
        
        auto second_half_vec{ std::ranges::to<std::vector>( second_half ) };
        EXPECT_EQ( bpt.insert_presorted( second_half_vec ), second_half_vec.size() );
        
        EXPECT_EQ( bpt.size(), static_cast<std::size_t>( test_size ) );
        EXPECT_TRUE( std::ranges::equal( bpt, sorted_numbers ) );
    }

    // Test 3: insert_presorted with interleaved data
    {
        bptree_set<int> bpt;
        bpt.map_memory( test_size );

        tr_vector<int> odds{ test_size / 2, no_init };
        for ( auto i{ 0U }; i < test_size / 2; ++i )
            odds[ i ] = i * 2 + 1;
        EXPECT_EQ( bpt.insert_presorted( odds ), odds.size() );

        tr_vector<int> evens{ test_size / 2, no_init };
        for ( auto i{ 0U }; i < test_size / 2; ++i )
            evens[ i ] = i * 2;
        EXPECT_EQ( bpt.insert_presorted( evens ), evens.size() );

        EXPECT_EQ( bpt.size(), static_cast<std::size_t>( test_size ) );
        EXPECT_TRUE( std::ranges::equal( bpt, sorted_numbers ) );
    }

    // Test 4: insert_presorted with duplicates (unique tree should skip them)
    {
        bptree_set<int> bpt;
        bpt.map_memory( test_size );

        auto sorted_vec{ std::ranges::to<std::vector>( sorted_numbers ) };
        EXPECT_EQ( bpt.insert_presorted( sorted_vec ), static_cast<std::size_t>( test_size ) );
        EXPECT_EQ( bpt.insert_presorted( sorted_vec ), 0 ); // all duplicates
        EXPECT_EQ( bpt.size(), static_cast<std::size_t>( test_size ) );
    }

    // Test 5: insert_presorted on multiset (non-unique)
    {
        bptree_multiset<int> bpt;
        bpt.map_memory( test_size * 2 );

        auto sorted_vec{ std::ranges::to<std::vector>( sorted_numbers ) };
        EXPECT_EQ( bpt.insert_presorted( sorted_vec ), static_cast<std::size_t>( test_size ) );
        EXPECT_EQ( bpt.insert_presorted( sorted_vec ), static_cast<std::size_t>( test_size ) );
        EXPECT_EQ( bpt.size(), static_cast<std::size_t>( test_size * 2 ) );
    }

    // Test 6: insert_presorted with empty input
    {
        bptree_set<int> bpt;
        bpt.map_memory();
        
        std::span<int const> empty_span;
        EXPECT_EQ( bpt.insert_presorted( empty_span ), 0 );
        EXPECT_TRUE( bpt.empty() );
    }

    // Test 7: insert_presorted with single element
    {
        bptree_set<int> bpt;
        bpt.map_memory();
        
        std::array<int, 1> single{ 42 };
        EXPECT_EQ( bpt.insert_presorted( single ), 1 );
        EXPECT_EQ( bpt.size(), 1 );
        EXPECT_NE( bpt.find( 42 ), bpt.end() );
    }
}

// these generated tests below need more work (do not actually test what they purport to)

TEST( bp_tree, insert_merge_at_node_boundary )
{
    // This test exercises the code path where merge() fills up a node completely,
    // returning tgt_next_offset == tgt_leaf->num_vals, which previously could
    // cause issues in find_next_insertion_point.
    
    bptree_set<int> bpt;
    bpt.map_memory();

    // First, insert values that will create a specific tree structure.
    // We want to create a situation where:
    // 1. A node gets filled to capacity during merge
    // 2. The next key to insert should go into the next node
    
    // Get the max values per node to craft our test data
    auto constexpr max_per_node{ decltype(bpt)::leaf_node::max_values };

    // Insert initial sorted data that fills exactly one node
    std::array<int, max_per_node> initial_data;
    for ( auto i{ 0U }; i < max_per_node; ++i )
        initial_data[ i ] = i * 2; // even numbers

    EXPECT_EQ( bpt.insert( initial_data ), initial_data.size() );

    // Now insert interleaved values that will:
    // 1. Fill up the first node to max capacity
    // 2. Need to continue into a new/split node
    std::array<int, max_per_node> interleaved_data;
    for ( auto i{ 0U }; i < max_per_node; ++i )
        interleaved_data[ i ] = i * 2 + 1; // odd numbers

    // This bulk insert should trigger the merge-at-node-boundary code path
    EXPECT_EQ( bpt.insert( interleaved_data ), interleaved_data.size() );
    
    // Verify the tree is correctly sorted
    EXPECT_TRUE( std::ranges::is_sorted( bpt, bpt.comp() ) );
    
    // Verify all values are present
    for ( auto v : initial_data )
        EXPECT_NE( bpt.find( v ), bpt.end() );
    for ( auto v : interleaved_data )
        EXPECT_NE( bpt.find( v ), bpt.end() );
}

TEST( bp_tree, insert_presorted_merge_at_node_boundary )
{
    // Same test but for insert_presorted
    
    bptree_set<int> bpt;
    bpt.map_memory();

    auto constexpr max_per_node{ decltype(bpt)::leaf_node::max_values };
    
    // Insert initial sorted data
    std::array<int, max_per_node> initial_data;
    for ( auto i{ 0U }; i < max_per_node; ++i )
        initial_data[ i ] = i * 2; // even numbers
    EXPECT_EQ( bpt.insert_presorted( initial_data ), initial_data.size() );
    
    // Insert interleaved presorted values
    std::array<int, max_per_node> interleaved_data;
    for ( auto i{ 0U }; i < max_per_node; ++i )
        interleaved_data[ i ] = i * 2 + 1; // odd numbers
    EXPECT_EQ( bpt.insert_presorted( interleaved_data ), interleaved_data.size() );
    
    EXPECT_TRUE( std::ranges::is_sorted( bpt, bpt.comp() ) );
    
    for ( auto v : initial_data )
        EXPECT_NE( bpt.find( v ), bpt.end() );
    for ( auto v : interleaved_data )
        EXPECT_NE( bpt.find( v ), bpt.end() );
}

TEST( bp_tree, insert_triggers_multiple_splits )
{
    // Test that exercises repeated splits during bulk insert,
    // ensuring node boundary handling works correctly across multiple splits
    
    bptree_set<int> bpt;
    bpt.map_memory();

    auto constexpr max_per_node{ decltype(bpt)::leaf_node::max_values };
    auto const test_size{ max_per_node * 5 }; // Enough to cause multiple splits
    
    // First insert: every 3rd number
    tr_vector<int> first_batch;
    for ( int i = 0; i < test_size; i += 3 )
        first_batch.push_back( i );

    EXPECT_EQ( bpt.insert( first_batch ), first_batch.size() );
    
    // Second insert: numbers that interleave with first batch
    tr_vector<int> second_batch;
    for ( int i = 0; i < test_size; ++i ) {
        if ( i % 3 != 0 )
            second_batch.push_back( i );
    }
    
    EXPECT_EQ( bpt.insert( second_batch ), second_batch.size() );
    
    // Verify correctness
    EXPECT_EQ( bpt.size(), static_cast<std::size_t>( test_size ) );
    EXPECT_TRUE( std::ranges::is_sorted( bpt, bpt.comp() ) );
    EXPECT_TRUE( std::ranges::equal( bpt, std::views::iota( 0, test_size ) ) );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
