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
            shuffled_even_numbers.insert( shuffled_even_numbers.end(), merge_appendix.begin(), merge_appendix.end() );
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
        bptree_set<unsigned> bpt;
        bpt.map_memory( test_size );

        tr_vector<unsigned> odds{ test_size / 2, no_init };
        for ( auto i{ 0U }; i < test_size / 2; ++i )
            odds[ i ] = i * 2 + 1;
        EXPECT_EQ( bpt.insert_presorted( odds ), odds.size() );

        tr_vector<unsigned> evens{ test_size / 2, no_init };
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
    
    bptree_set<unsigned> bpt;
    bpt.map_memory();

    // First, insert values that will create a specific tree structure.
    // We want to create a situation where:
    // 1. A node gets filled to capacity during merge
    // 2. The next key to insert should go into the next node
    
    // Get the max values per node to craft our test data
    auto constexpr max_per_node{ decltype(bpt)::leaf_node::max_values };

    // Insert initial sorted data that fills exactly one node
    std::array<unsigned, max_per_node> initial_data;
    for ( auto i{ 0U }; i < max_per_node; ++i )
        initial_data[ i ] = i * 2; // even numbers

    EXPECT_EQ( bpt.insert( initial_data ), initial_data.size() );

    // Now insert interleaved values that will:
    // 1. Fill up the first node to max capacity
    // 2. Need to continue into a new/split node
    std::array<unsigned, max_per_node> interleaved_data;
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
    
    bptree_set<unsigned> bpt;
    bpt.map_memory();

    auto constexpr max_per_node{ decltype(bpt)::leaf_node::max_values };
    
    // Insert initial sorted data
    std::array<unsigned, max_per_node> initial_data;
    for ( auto i{ 0U }; i < max_per_node; ++i )
        initial_data[ i ] = i * 2; // even numbers
    EXPECT_EQ( bpt.insert_presorted( initial_data ), initial_data.size() );
    
    // Insert interleaved presorted values
    std::array<unsigned, max_per_node> interleaved_data;
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

    bptree_set<unsigned> bpt;
    bpt.map_memory();

    auto constexpr max_per_node{ decltype(bpt)::leaf_node::max_values };
    auto const test_size{ max_per_node * 5 }; // Enough to cause multiple splits

    // First insert: every 3rd number
    tr_vector<unsigned> first_batch;
    for ( auto i{ 0U }; i < test_size; i += 3 )
        first_batch.push_back( i );

    EXPECT_EQ( bpt.insert( first_batch ), first_batch.size() );

    // Second insert: numbers that interleave with first batch
    tr_vector<unsigned> second_batch;
    for ( auto i{ 0U }; i < test_size; ++i ) {
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
// Helper to verify tree invariants
//------------------------------------------------------------------------------
template <typename BPTree>
void verify_invariants( BPTree const & bpt, std::string_view context = "" )
{
    // 1. Tree must be sorted according to its comparator
    EXPECT_TRUE( std::ranges::is_sorted( bpt, bpt.comp() ) )
        << "Tree is not sorted" << ( context.empty() ? "" : " after " ) << context;

    // 2. Size must match iteration count
    auto const iteration_count{ static_cast<std::size_t>( std::distance( bpt.begin(), bpt.end() ) ) };
    EXPECT_EQ( bpt.size(), iteration_count )
        << "Size mismatch" << ( context.empty() ? "" : " after " ) << context;

    // 3. Random access iteration must also match
    auto const ra_count{ static_cast<std::size_t>( std::distance( bpt.ra_begin(), bpt.ra_end() ) ) };
    EXPECT_EQ( bpt.size(), ra_count )
        << "Random access size mismatch" << ( context.empty() ? "" : " after " ) << context;

    // 4. Forward and random access iterations must yield same values
    if ( !bpt.empty() ) {
        EXPECT_TRUE( std::ranges::equal( bpt, bpt.random_access() ) )
            << "Forward and random access iterators differ" << ( context.empty() ? "" : " after " ) << context;
    }
}


namespace {
    std::vector<int> indirect_values;

    struct indirect_comparator {
        using is_transparent = std::true_type; // Enable heterogeneous lookup

        bool operator()( unsigned const a, unsigned const b ) const noexcept {
            return indirect_values[ a ] < indirect_values[ b ];
        }
    };
} // anonymous namespace

TEST( bp_tree, replace_keys_inplace_basic )
{
    // Test replace_keys_inplace with a comparator that uses external values.
    // This simulates the use case where only indirect indices change of otherwise
    // same values.
    using tree_type = psi::vm::bp_tree<unsigned, true, indirect_comparator>;
    tree_type bpt;
    bpt.map_memory();

    // Setup: 10 rows with values 0, 10, 20, 30, ...
    indirect_values.resize( 20 );
    for ( auto i{ 0U }; i < 10; ++i )
        indirect_values[ i ] = static_cast<int>( i * 10 );

    // Insert row indices 0..9
    std::vector<unsigned> row_indices{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    bpt.insert( row_indices );
    EXPECT_EQ( bpt.size(), 10 );

    // Simulate row compaction: rows 3, 5, 7 get new indices 10, 11, 12
    // but they still have the same values (30, 50, 70)
    indirect_values[ 10 ] = 30; // same as row 3
    indirect_values[ 11 ] = 50; // same as row 5
    indirect_values[ 12 ] = 70; // same as row 7

    // Replace old row indices with new ones
    std::array<unsigned, 3> old_rows{ 3, 5, 7 };
    std::array<unsigned, 3> new_rows{ 10, 11, 12 };

    EXPECT_EQ( bpt.replace_keys_inplace( old_rows, new_rows ), 3 );

    // With custom comparator, find() uses value-based lookup, so find(3) will
    // still find an entry (key 10 has the same value). We need to verify the
    // ACTUAL stored keys by looking at what the iterator points to.
    auto it3 = bpt.find( 3U );
    ASSERT_NE( it3, bpt.end() ); // Found entry with value 30
    EXPECT_EQ( *it3, 10U );      // But the stored key should be 10, not 3

    auto it5 = bpt.find( 5U );
    ASSERT_NE( it5, bpt.end() ); // Found entry with value 50
    EXPECT_EQ( *it5, 11U );      // But the stored key should be 11, not 5

    auto it7 = bpt.find( 7U );
    ASSERT_NE( it7, bpt.end() ); // Found entry with value 70
    EXPECT_EQ( *it7, 12U );      // But the stored key should be 12, not 7

    // Size should remain the same
    EXPECT_EQ( bpt.size(), 10 );

    // Also verify by collecting all keys
    std::set<unsigned> expected{ 0, 1, 2, 10, 4, 11, 6, 12, 8, 9 };
    std::set<unsigned> actual;
    for ( auto key : bpt )
        actual.insert( key );
    EXPECT_EQ( actual, expected );

    indirect_values.clear();
}

TEST( bp_tree, replace_keys_inplace_empty_input )
{
    bptree_set<int> bpt;
    bpt.map_memory();

    std::ranges::iota_view const numbers{ 0, 50 };
    auto nums{ std::ranges::to<std::vector>( numbers ) };
    bpt.insert( nums );

    std::span<int const> empty_old;
    std::span<int const> empty_new;

    EXPECT_EQ( bpt.replace_keys_inplace( empty_old, empty_new ), 0 );
    EXPECT_EQ( bpt.size(), 50 );
}

TEST( bp_tree, replace_keys_inplace_empty_tree )
{
    using tree_type = psi::vm::bp_tree<unsigned, true, indirect_comparator>;
    tree_type bpt;
    bpt.map_memory();

    if ( !bpt.all_bulk_erase_keys_must_exist )
    {
        indirect_values = { 10, 20 };  // values for rows 0 and 1

        std::array<unsigned, 2> old_keys{ 0, 1 };
        std::array<unsigned, 2> new_keys{ 0, 1 }; // same (tree is empty, won't find them anyway)

        // Should return 0 and not crash on empty tree
        EXPECT_EQ( bpt.replace_keys_inplace( old_keys, new_keys ), 0 );

        indirect_values.clear();
    }
    EXPECT_TRUE( bpt.empty() );
}

TEST( bp_tree, replace_keys_inplace_all_keys )
{
    // Replace all keys in the tree with equivalent-comparing new keys
    using tree_type = psi::vm::bp_tree<unsigned, true, indirect_comparator>;
    tree_type bpt;
    bpt.map_memory();

    auto const test_size{ 100U };

    // Setup values: rows 0..99 have values 0..99, rows 100..199 have same values
    indirect_values.resize( test_size * 2 );
    for ( auto i{ 0U }; i < test_size; ++i ) {
        indirect_values[ i ] = static_cast<int>( i );
        indirect_values[ i + test_size ] = static_cast<int>( i ); // same values
    }

    std::vector<unsigned> old_keys( test_size );
    std::vector<unsigned> new_keys( test_size );
    for ( auto i{ 0U }; i < test_size; ++i ) {
        old_keys[ i ] = i;
        new_keys[ i ] = i + test_size; // new indices with same values
    }

    bpt.insert( old_keys );
    EXPECT_EQ( bpt.replace_keys_inplace( old_keys, new_keys ), test_size );

    // Verify all keys are replaced by checking actual stored values
    // With the custom comparator, find(old_key) finds entries with same value,
    // but the stored key should be the new key.
    for ( auto i{ 0U }; i < test_size; ++i ) {
        auto it = bpt.find( i ); // Finds entry with value i
        ASSERT_NE( it, bpt.end() );
        EXPECT_EQ( *it, i + test_size ); // Stored key should be new, not old
    }
    EXPECT_EQ( bpt.size(), test_size );

    indirect_values.clear();
}

TEST( bp_tree, replace_keys_inplace_large_tree )
{
    // Test with a larger tree that spans multiple leaves
    using tree_type = psi::vm::bp_tree<unsigned, true, indirect_comparator>;
    tree_type bpt;
    bpt.map_memory();

    auto constexpr max_per_node{ tree_type::leaf_node::max_values };
    auto const test_size{ max_per_node * 10 }; // Multiple leaves
    auto const offset{ test_size }; // new indices start here

    // Setup values
    indirect_values.resize( test_size * 2 );
    for ( auto i{ 0U }; i < test_size; ++i ) {
        indirect_values[ i ] = static_cast<int>( i );
        indirect_values[ i + offset ] = static_cast<int>( i ); // same values
    }

    std::vector<unsigned> nums( test_size );
    for ( auto i{ 0U }; i < test_size; ++i )
        nums[ i ] = i;

    bpt.insert_presorted( nums );

    // Replace every 10th key
    std::vector<unsigned> old_keys;
    std::vector<unsigned> new_keys;
    for ( auto i{ 0U }; i < test_size; i += 10 ) {
        old_keys.push_back( i );
        new_keys.push_back( i + offset );
    }

    EXPECT_EQ( bpt.replace_keys_inplace( old_keys, new_keys ), old_keys.size() );

    // Verify replacements - check actual stored keys
    for ( size_t i{ 0 }; i < old_keys.size(); ++i ) {
        auto it = bpt.find( old_keys[ i ] ); // Finds by value
        ASSERT_NE( it, bpt.end() );
        EXPECT_EQ( *it, new_keys[ i ] ); // Stored key should be new
    }

    // Verify unchanged keys are still present with original indices
    for ( auto i{ 1U }; i < test_size; ++i ) {
        if ( i % 10 != 0 ) {
            auto it = bpt.find( i );
            ASSERT_NE( it, bpt.end() );
            EXPECT_EQ( *it, i ); // Unchanged keys retain original index
        }
    }

    indirect_values.clear();
}

TEST( bp_tree, replace_keys_inplace_single_key )
{
    using tree_type = psi::vm::bp_tree<unsigned, true, indirect_comparator>;
    tree_type bpt;
    bpt.map_memory();

    // Values: row 0-4 have values 10,20,30,40,50. Row 10 has value 30 (same as row 2)
    indirect_values = { 10, 20, 30, 40, 50, 0, 0, 0, 0, 0, 30 };

    bpt.insert( { 0U, 1U, 2U, 3U, 4U } );

    std::array<unsigned, 1> old_key{ 2U };
    std::array<unsigned, 1> new_key{ 10U }; // new index with same value (30)

    EXPECT_EQ( bpt.replace_keys_inplace( old_key, new_key ), 1 );

    // Verify the stored key changed from 2 to 10
    auto it = bpt.find( 2U ); // Finds entry with value 30
    ASSERT_NE( it, bpt.end() );
    EXPECT_EQ( *it, 10U ); // But stored key should be 10, not 2

    EXPECT_EQ( bpt.size(), 5 );

    indirect_values.clear();
}

//------------------------------------------------------------------------------
// Tests for erase_sorted (bulk removal for unique btrees)
//------------------------------------------------------------------------------

TEST( bp_tree, erase_sorted_basic )
{
    bptree_set<int> bpt;
    bpt.map_memory();

    // Insert 0..99
    std::ranges::iota_view const numbers{ 0, 100 };
    auto nums{ std::ranges::to<std::vector>( numbers ) };
    bpt.insert( nums );

    // Remove keys 10, 20, 30 (sorted)
    std::array<int, 3> keys_to_remove{ 10, 20, 30 };
    EXPECT_EQ( bpt.erase_sorted( keys_to_remove ), 3 );

    // Verify keys are removed
    EXPECT_EQ( bpt.find( 10 ), bpt.end() );
    EXPECT_EQ( bpt.find( 20 ), bpt.end() );
    EXPECT_EQ( bpt.find( 30 ), bpt.end() );

    // Verify other keys are still present
    EXPECT_NE( bpt.find( 0 ), bpt.end() );
    EXPECT_NE( bpt.find( 99 ), bpt.end() );
    EXPECT_NE( bpt.find( 50 ), bpt.end() );

    EXPECT_EQ( bpt.size(), 97 );

    verify_invariants( bpt, "erase_sorted basic" );
}

TEST( bp_tree, erase_sorted_empty_input )
{
    bptree_set<int> bpt;
    bpt.map_memory();

    bpt.insert( { 1, 2, 3, 4, 5 } );

    std::span<int const> empty_keys;
    EXPECT_EQ( bpt.erase_sorted( empty_keys ), 0 );
    EXPECT_EQ( bpt.size(), 5 );
}

TEST( bp_tree, erase_sorted_empty_tree )
{
    bptree_set<int> bpt;
    bpt.map_memory();
    if ( !bpt.all_bulk_erase_keys_must_exist )
    {
        std::array<int, 2> keys{ 1, 2 };
        EXPECT_EQ( bpt.erase_sorted( keys ), 0 );
    }
    EXPECT_TRUE( bpt.empty() );
}

TEST( bp_tree, erase_sorted_all_keys )
{
    bptree_set<unsigned> bpt;
    bpt.map_memory();

    auto const test_size{ 500U };
    std::vector<unsigned> nums( test_size );
    for ( auto i{ 0U }; i < test_size; ++i )
        nums[ i ] = i;

    bpt.insert( nums );
    EXPECT_EQ( bpt.erase_sorted( nums ), test_size );
    EXPECT_TRUE( bpt.empty() );
}

TEST( bp_tree, erase_sorted_large_tree )
{
    // Test with a larger tree spanning multiple leaves
    bptree_set<unsigned> bpt;
    bpt.map_memory();

    auto constexpr max_per_node{ decltype(bpt)::leaf_node::max_values };
    auto const test_size{ max_per_node * 10U };

    std::vector<unsigned> nums( test_size );
    for ( auto i{ 0U }; i < test_size; ++i )
        nums[ i ] = i;

    bpt.insert_presorted( nums );

    // Remove every 5th key
    std::vector<unsigned> keys_to_remove;
    for ( auto i{ 0U }; i < test_size; i += 5 )
        keys_to_remove.push_back( i );

    EXPECT_EQ( bpt.erase_sorted( keys_to_remove ), keys_to_remove.size() );

    // Verify
    for ( auto k : keys_to_remove )
        EXPECT_EQ( bpt.find( k ), bpt.end() );

    // Verify remaining keys
    for ( auto i{ 0U }; i < test_size; ++i ) {
        if ( i % 5 != 0 )
            EXPECT_NE( bpt.find( i ), bpt.end() );
    }

    EXPECT_EQ( bpt.size(), test_size - keys_to_remove.size() );
    verify_invariants( bpt, "erase_sorted large tree" );
}

TEST( bp_tree, erase_sorted_nonexistent_keys )
{
    bptree_set<int> bpt;
    bpt.map_memory();

    bpt.insert( { 2, 4, 6, 8, 10 } );

    // Try to remove keys that don't exist (odd numbers)
    std::array<int, 3> nonexistent{ 1, 3, 5 };
    EXPECT_EQ( bpt.erase_sorted( nonexistent ), 0 );
    EXPECT_EQ( bpt.size(), 5 );
}

TEST( bp_tree, erase_sorted_mixed_existing_nonexisting )
{
    bptree_set<int> bpt;
    bpt.map_memory();

    bpt.insert( { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 } );

    // Mix of existing (2, 4, 6) and non-existing (11, 12)
    std::array<int, 5> keys{ 2, 4, 6, 11, 12 };
    EXPECT_EQ( bpt.erase_sorted( keys ), 3 ); // Only 3 exist

    EXPECT_EQ( bpt.find( 2 ), bpt.end() );
    EXPECT_EQ( bpt.find( 4 ), bpt.end() );
    EXPECT_EQ( bpt.find( 6 ), bpt.end() );
    EXPECT_NE( bpt.find( 1 ), bpt.end() );
    EXPECT_NE( bpt.find( 3 ), bpt.end() );
    EXPECT_EQ( bpt.size(), 7 );

    verify_invariants( bpt, "erase_sorted mixed" );
}

TEST( bp_tree, erase_sorted_single_key )
{
    bptree_set<int> bpt;
    bpt.map_memory();

    bpt.insert( { 1, 2, 3, 4, 5 } );

    std::array<int, 1> key{ 3 };
    EXPECT_EQ( bpt.erase_sorted( key ), 1 );
    EXPECT_EQ( bpt.find( 3 ), bpt.end() );
    EXPECT_EQ( bpt.size(), 4 );
}

TEST( bp_tree, erase_sorted_triggers_underflow )
{
    // Test that underflow handling works correctly
    bptree_set<unsigned> bpt;
    bpt.map_memory();

    auto constexpr max_per_node{ decltype(bpt)::leaf_node::max_values };
    auto constexpr min_per_node{ decltype(bpt)::leaf_node::min_values };

    // Create tree with multiple leaves
    auto const test_size{ max_per_node * 3U };
    std::vector<unsigned> nums( test_size );
    for ( auto i{ 0U }; i < test_size; ++i )
        nums[ i ] = i;

    bpt.insert_presorted( nums );

    // Remove enough keys from middle to trigger underflow
    std::vector<unsigned> keys_to_remove;
    for ( auto i{ max_per_node }; i < max_per_node + min_per_node + 1; ++i )
        keys_to_remove.push_back( i );

    auto const removed{ bpt.erase_sorted( keys_to_remove ) };
    EXPECT_EQ( removed, keys_to_remove.size() );

    // Tree should still be valid - verify all invariants
    verify_invariants( bpt, "erase_sorted underflow" );
    EXPECT_EQ( bpt.size(), test_size - keys_to_remove.size() );

    // Verify removed keys are gone
    for ( auto k : keys_to_remove )
        EXPECT_EQ( bpt.find( k ), bpt.end() );

    // Verify remaining keys are present
    for ( auto i{ 0U }; i < test_size; ++i ) {
        bool const was_removed{ i >= max_per_node && i < max_per_node + min_per_node + 1 };
        if ( !was_removed )
            EXPECT_NE( bpt.find( i ), bpt.end() );
    }
}

TEST( bp_tree, erase_sorted_first_leaf )
{
    // Test removing all keys from the first leaf (which has no left sibling)
    bptree_set<unsigned> bpt;
    bpt.map_memory();

    auto constexpr max_per_node{ decltype(bpt)::leaf_node::max_values };

    // Create tree with multiple leaves
    auto const test_size{ max_per_node * 3U };
    std::vector<unsigned> nums( test_size );
    for ( auto i{ 0U }; i < test_size; ++i )
        nums[ i ] = i;

    bpt.insert_presorted( nums );

    // Remove all keys from the first leaf (keys 0 to max_per_node-1)
    std::vector<unsigned> keys_to_remove;
    for ( auto i{ 0U }; i < max_per_node; ++i )
        keys_to_remove.push_back( i );

    auto const removed{ bpt.erase_sorted( keys_to_remove ) };
    EXPECT_EQ( removed, keys_to_remove.size() );

    // Tree should still be valid - verify all invariants
    verify_invariants( bpt, "erase_sorted first leaf" );
    EXPECT_EQ( bpt.size(), test_size - keys_to_remove.size() );

    // Verify removed keys are gone
    for ( auto k : keys_to_remove )
        EXPECT_EQ( bpt.find( k ), bpt.end() );

    // Verify remaining keys are present
    for ( auto i{ max_per_node }; i < test_size; ++i ) {
        EXPECT_NE( bpt.find( i ), bpt.end() );
    }
}

TEST( bp_tree, erase_sorted_from_beginning )
{
    // Test removing keys from the beginning of the tree
    bptree_set<unsigned> bpt;
    bpt.map_memory();

    auto constexpr max_per_node{ decltype(bpt)::leaf_node::max_values };

    // Create tree with multiple leaves
    auto const test_size{ max_per_node * 4U };
    std::vector<unsigned> nums( test_size );
    for ( auto i{ 0U }; i < test_size; ++i )
        nums[ i ] = i;

    bpt.insert_presorted( nums );

    // Remove the first few keys (within first leaf, but not all of them)
    std::vector<unsigned> keys_to_remove;
    for ( auto i{ 0U }; i < max_per_node / 2; ++i )
        keys_to_remove.push_back( i );

    auto const removed{ bpt.erase_sorted( keys_to_remove ) };
    EXPECT_EQ( removed, keys_to_remove.size() );

    verify_invariants( bpt, "erase_sorted from beginning" );
    EXPECT_EQ( bpt.size(), test_size - keys_to_remove.size() );
}

TEST( bp_tree, erase_sorted_single_leaf_tree )
{
    // Test erase_sorted on a tree with a single leaf (which is also the root)
    bptree_set<int> bpt;
    bpt.map_memory();

    // Insert just a few elements (will fit in single leaf)
    bpt.insert( { 1, 2, 3, 4, 5 } );

    // Remove some keys
    std::array<int, 2> keys{ 2, 4 };
    EXPECT_EQ( bpt.erase_sorted( keys ), 2 );

    verify_invariants( bpt, "erase_sorted single leaf" );
    EXPECT_EQ( bpt.size(), 3 );

    // Remove remaining keys
    std::array<int, 3> remaining{ 1, 3, 5 };
    EXPECT_EQ( bpt.erase_sorted( remaining ), 3 );
    EXPECT_TRUE( bpt.empty() );
}

//------------------------------------------------------------------------------
// Tests for erase_sorted_exact (bulk removal requiring exact key equality)
//------------------------------------------------------------------------------

TEST( bp_tree, erase_sorted_exact_basic )
{
    // Basic test: erase_sorted_exact should work like erase_sorted for simple types
    bptree_set<int> bpt;
    bpt.map_memory();

    // Insert 0..99
    std::ranges::iota_view const numbers{ 0, 100 };
    auto nums{ std::ranges::to<std::vector>( numbers ) };
    bpt.insert( nums );

    // Remove keys 10, 20, 30 (sorted)
    std::array<int, 3> keys_to_remove{ 10, 20, 30 };
    EXPECT_EQ( bpt.erase_sorted_exact( keys_to_remove ), 3 );

    // Verify keys are removed
    EXPECT_EQ( bpt.find( 10 ), bpt.end() );
    EXPECT_EQ( bpt.find( 20 ), bpt.end() );
    EXPECT_EQ( bpt.find( 30 ), bpt.end() );

    EXPECT_EQ( bpt.size(), 97 );
    verify_invariants( bpt, "erase_sorted_exact basic" );
}

TEST( bp_tree, erase_sorted_exact_with_indirect_comparator )
{
    // Test the key feature: with indirect comparator, equivalent but non-equal keys
    // should NOT be erased.
    using tree_type = psi::vm::bp_tree<unsigned, true, indirect_comparator>;
    tree_type bpt;
    bpt.map_memory();

    // Setup: rows 0-4 have values 10, 20, 30, 40, 50
    // Rows 10-14 have the SAME values (for testing equivalent but not equal keys)
    indirect_values.resize( 20 );
    indirect_values[ 0 ] = 10;
    indirect_values[ 1 ] = 20;
    indirect_values[ 2 ] = 30;
    indirect_values[ 3 ] = 40;
    indirect_values[ 4 ] = 50;
    indirect_values[ 10 ] = 10;  // same value as row 0
    indirect_values[ 11 ] = 20;  // same value as row 1
    indirect_values[ 12 ] = 30;  // same value as row 2

    // Insert row indices 0-4
    bpt.insert( { 0U, 1U, 2U, 3U, 4U } );
    EXPECT_EQ( bpt.size(), 5 );

    // Try to erase rows 10, 11, 12 which have the SAME VALUES but are NOT in the tree
    // erase_sorted_exact should NOT erase anything because the actual keys (10, 11, 12)
    // are not present, only keys with equivalent values (0, 1, 2) are present.
    std::array<unsigned, 3> wrong_keys{ 10U, 11U, 12U };
    EXPECT_EQ( bpt.erase_sorted_exact( wrong_keys ), 0 );

    // Tree should still have all 5 elements
    EXPECT_EQ( bpt.size(), 5 );
    EXPECT_NE( bpt.find( 0U ), bpt.end() );
    EXPECT_NE( bpt.find( 1U ), bpt.end() );
    EXPECT_NE( bpt.find( 2U ), bpt.end() );

    // Now erase the actual keys that ARE in the tree
    std::array<unsigned, 2> correct_keys{ 1U, 3U };
    EXPECT_EQ( bpt.erase_sorted_exact( correct_keys ), 2 );

    EXPECT_EQ( bpt.size(), 3 );
    EXPECT_NE( bpt.find( 0U ), bpt.end() );  // still there
    EXPECT_EQ( bpt.find( 1U ), bpt.end() );  // removed
    EXPECT_NE( bpt.find( 2U ), bpt.end() );  // still there
    EXPECT_EQ( bpt.find( 3U ), bpt.end() );  // removed
    EXPECT_NE( bpt.find( 4U ), bpt.end() );  // still there

    indirect_values.clear();
}

TEST( bp_tree, erase_sorted_exact_vs_erase_sorted_comparison )
{
    // Demonstrate the difference between erase_sorted and erase_sorted_exact
    // with an indirect comparator
    using tree_type = psi::vm::bp_tree<unsigned, true, indirect_comparator>;

    // Setup values: rows 0-2 have values 100, 200, 300
    // rows 5-7 have SAME values 100, 200, 300
    indirect_values.resize( 10 );
    indirect_values[ 0 ] = 100;
    indirect_values[ 1 ] = 200;
    indirect_values[ 2 ] = 300;
    indirect_values[ 5 ] = 100;  // same value as row 0
    indirect_values[ 6 ] = 200;  // same value as row 1
    indirect_values[ 7 ] = 300;  // same value as row 2

    // Test 1: erase_sorted would erase by comparison (finds equivalent key)
    {
        tree_type bpt;
        bpt.map_memory();
        bpt.insert( { 0U, 1U, 2U } );
        EXPECT_EQ( bpt.size(), 3 );

        // Try to erase row 5 - with erase_sorted this would erase row 0
        // (because they compare as equivalent)
        std::array<unsigned, 1> key{ 5U };
        auto const erased = bpt.erase_sorted( key );
        // erase_sorted erases by comparison, so it finds row 0 (same value) and erases it
        EXPECT_EQ( erased, 1 );
        EXPECT_EQ( bpt.size(), 2 );
    }

    // Test 2: erase_sorted_exact only erases exact key matches
    {
        tree_type bpt;
        bpt.map_memory();
        bpt.insert( { 0U, 1U, 2U } );
        EXPECT_EQ( bpt.size(), 3 );

        // Try to erase row 5 - with erase_sorted_exact this should NOT erase anything
        // because row 5 is not in the tree (only row 0 with the same value is)
        std::array<unsigned, 1> key{ 5U };
        auto const erased = bpt.erase_sorted_exact( key );
        EXPECT_EQ( erased, 0 );
        EXPECT_EQ( bpt.size(), 3 );  // unchanged
    }

    indirect_values.clear();
}

TEST( bp_tree, erase_sorted_exact_large_tree )
{
    // Test with a larger tree spanning multiple leaves
    using tree_type = psi::vm::bp_tree<unsigned, true, indirect_comparator>;
    tree_type bpt;
    bpt.map_memory();

    auto constexpr max_per_node{ tree_type::leaf_node::max_values };
    auto const test_size{ max_per_node * 5U };
    auto const offset{ test_size }; // for creating equivalent but non-equal keys

    // Setup values: rows 0..test_size-1 have sequential values
    // rows test_size..2*test_size-1 have the SAME values
    indirect_values.resize( test_size * 2 );
    for ( auto i{ 0U }; i < test_size; ++i ) {
        indirect_values[ i ] = static_cast<int>( i * 10 );
        indirect_values[ i + offset ] = static_cast<int>( i * 10 );  // same values
    }

    // Insert rows 0..test_size-1
    std::vector<unsigned> keys( test_size );
    for ( auto i{ 0U }; i < test_size; ++i )
        keys[ i ] = i;
    bpt.insert( keys );

    // Try to erase equivalent but non-existent keys (offset..offset+test_size/10)
    std::vector<unsigned> wrong_keys;
    for ( auto i{ 0U }; i < test_size / 10; ++i )
        wrong_keys.push_back( i + offset );

    EXPECT_EQ( bpt.erase_sorted_exact( wrong_keys ), 0 );
    EXPECT_EQ( bpt.size(), test_size );

    // Now erase actual keys
    std::vector<unsigned> correct_keys;
    for ( auto i{ 0U }; i < test_size; i += 10 )
        correct_keys.push_back( i );

    EXPECT_EQ( bpt.erase_sorted_exact( correct_keys ), correct_keys.size() );
    EXPECT_EQ( bpt.size(), test_size - correct_keys.size() );

    verify_invariants( bpt, "erase_sorted_exact large tree" );

    indirect_values.clear();
}

TEST( bp_tree, erase_sorted_exact_mixed_present_absent )
{
    // Test with a mix of present and absent keys
    using tree_type = psi::vm::bp_tree<unsigned, true, indirect_comparator>;
    tree_type bpt;
    bpt.map_memory();

    // Values: rows 0-9 have values 0-9, rows 20-29 have same values
    indirect_values.resize( 30 );
    for ( auto i{ 0U }; i < 10; ++i ) {
        indirect_values[ i ] = static_cast<int>( i );
        indirect_values[ i + 20 ] = static_cast<int>( i );  // same values
    }

    // Insert rows 0-9
    bpt.insert( { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U } );

    // Try to erase: 2 (present), 22 (absent but equivalent), 4 (present), 24 (absent but equivalent)
    // Need to sort by comparator (which compares by value)
    // 2 has value 2, 22 has value 2, 4 has value 4, 24 has value 4
    // Sorted by comparator: 2 (val 2), 22 (val 2), 4 (val 4), 24 (val 4)
    std::array<unsigned, 4> mixed_keys{ 2U, 22U, 4U, 24U };

    // Only exact matches should be erased: 2 and 4
    EXPECT_EQ( bpt.erase_sorted_exact( mixed_keys ), 2 );
    EXPECT_EQ( bpt.size(), 8 );

    EXPECT_EQ( bpt.find( 2U ), bpt.end() );  // removed
    EXPECT_EQ( bpt.find( 4U ), bpt.end() );  // removed
    EXPECT_NE( bpt.find( 0U ), bpt.end() );  // still there

    indirect_values.clear();
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
