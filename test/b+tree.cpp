#include <psi/vm/containers/b+tree.hpp>
#include <psi/vm/containers/b+tree_print.hpp>

#include <boost/assert.hpp>

#include <gtest/gtest.h>

#include <random>
#include <ranges>
#include <utility>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

static auto const test_file{ "test.bpt" };

TEST( bp_tree, playground )
{
#ifdef NDEBUG
    auto const test_size{ 14553735 };
#else
    auto const test_size{   258735 };
#endif
    std::ranges::iota_view constexpr sorted_numbers{ 0, test_size };
    std::mt19937 rng{ std::random_device{}() };
    auto numbers{ std::ranges::to<std::vector>( sorted_numbers ) };
    std::span const nums{ numbers };
    // leave the largest quarter of values at the end to trigger/exercise the
    // bulk_append branch in insert()
    std::ranges::shuffle( nums.subspan( 0, 3 * nums.size() / 4 ), rng );
    std::ranges::shuffle( nums.subspan( 3 * nums.size() / 4    ), rng );
    {
        bp_tree<int> bpt;
        bpt.map_memory();
        bpt.reserve( nums.size() );
        {
            auto const third{ nums.size() / 3 };
            auto const  first_third{ nums.subspan( 0 * third, third ) };
            auto const second_third{ nums.subspan( 1 * third, third ) };
            auto const  third_third{ nums.subspan( 2 * third        ) };
                                                    bpt.insert( first_third ); // bulk into empty
            for ( auto const & n : second_third ) { bpt.insert( n ); }         // single value
                                                    bpt.insert( third_third ); // bulk into non-empty
        }

        static_assert( std::forward_iterator<bp_tree<int>::const_iterator> );

        EXPECT_EQ  ( std::distance( bpt.   begin(), bpt.   end() ), bpt.size() );
        EXPECT_EQ  ( std::distance( bpt.ra_begin(), bpt.ra_end() ), bpt.size() );
        EXPECT_TRUE( std::ranges::is_sorted( std::as_const( bpt ), bpt.comp() ) );
        EXPECT_TRUE( std::ranges::equal( sorted_numbers, bpt                 ) );
        EXPECT_TRUE( std::ranges::equal( sorted_numbers, bpt.random_access() ) );
        EXPECT_NE( bpt.find( +42 ), bpt.end() );
        EXPECT_EQ( bpt.find( -42 ), bpt.end() );
        bpt.erase( 42 );
        EXPECT_EQ( bpt.find( +42 ), bpt.end() );
        bpt.insert( 42 );
        EXPECT_TRUE( std::ranges::equal(                      bpt.random_access()  ,                      sorted_numbers   ) );
        EXPECT_TRUE( std::ranges::equal( std::views::reverse( bpt.random_access() ), std::views::reverse( sorted_numbers ) ) );
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
                bpt.erase( n );
            }

            bp_tree<int> bpt_even;
            bpt_even.map_memory();
            shuffled_even_numbers.append_range( merge_appendix );
            bpt_even.insert( shuffled_even_numbers );

            bpt.merge( bpt_even );
        }

        EXPECT_TRUE( std::ranges::equal( std::ranges::iota_view{ 0, test_size + extra_entries_for_tree_merge }, bpt ) );

        std::shuffle( numbers.begin(), numbers.end(), rng );
        for ( auto const & n : numbers )
            bpt.erase( n );
        for ( auto const & n : merge_appendix )
            bpt.erase( n );

        EXPECT_TRUE( bpt.empty() );
    }

    {
        bp_tree<int> bpt;
        bpt.map_file( test_file, flags::named_object_construction_policy::create_new_or_truncate_existing );    

        for ( auto const & n : numbers )
            bpt.insert( n );
    
        static_assert( std::forward_iterator<bp_tree<int>::const_iterator> );

        EXPECT_TRUE( std::ranges::is_sorted( std::as_const( bpt ), bpt.comp() ) );
        EXPECT_TRUE( std::ranges::equal( bpt, sorted_numbers ) );
        EXPECT_NE( bpt.find( +42 ), bpt.end() );
        EXPECT_EQ( bpt.find( -42 ), bpt.end() );
        bpt.erase( 42 );
        EXPECT_EQ( bpt.find( +42 ), bpt.end() );
    }
    {
        bp_tree<int> bpt;
        bpt.map_file( test_file, flags::named_object_construction_policy::open_existing );    

        EXPECT_EQ( bpt.size(), sorted_numbers.size() - 1 );
        bpt.insert( +42 );
    
        EXPECT_TRUE( std::ranges::is_sorted( bpt, bpt.comp() ) );
        EXPECT_TRUE( std::ranges::equal( std::as_const( bpt ), sorted_numbers ) );
        EXPECT_NE( bpt.find( +42 ), bpt.end() );
        EXPECT_EQ( bpt.find( -42 ), bpt.end() );

        bpt.clear();
        bpt.print();
    }
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
