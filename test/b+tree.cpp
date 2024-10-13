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
    auto const test_size{ 3553735 };
#else
    auto const test_size{  158735 };
#endif
    std::ranges::iota_view constexpr sorted_numbers{ 0, test_size };
    std::mt19937 rng{ std::random_device{}() };
    auto numbers{ std::ranges::to<std::vector>( sorted_numbers ) };
    std::shuffle( numbers.begin(), numbers.end(), rng );
    {
        bp_tree<int> bpt;
        bpt.map_memory();
        bpt.reserve( numbers.size() );
        {
            auto const nums { std::span{ numbers } };
            auto const third{ nums.size() / 3 };
            auto const  first_third{ nums.subspan( 0 * third, third ) };
            auto const second_third{ nums.subspan( 1 * third, third ) };
            auto const  third_third{ nums.subspan( 2 * third        ) };
                                                    bpt.insert( first_third );
            for ( auto const & n : second_third ) { bpt.insert( n ); }
                                                    bpt.insert( third_third );
        }

        static_assert( std::forward_iterator<bp_tree<int>::const_iterator> );

        EXPECT_TRUE( std::ranges::is_sorted( std::as_const( bpt ) ) );
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
            for ( auto n : std::views::iota( 0, 45678 ) ) // slow operation: use a smaller subset of the input
                EXPECT_EQ( ra[ n ], n );
        }

        std::shuffle( numbers.begin(), numbers.end(), rng );
        for ( auto const & n : numbers )
            bpt.erase( n );

        EXPECT_TRUE( bpt.empty() );
    }

    {
        bp_tree<int> bpt;
        bpt.map_file( test_file, flags::named_object_construction_policy::create_new_or_truncate_existing );    

        for ( auto const & n : numbers )
            bpt.insert( n );
    
        static_assert( std::forward_iterator<bp_tree<int>::const_iterator> );

        EXPECT_TRUE( std::ranges::is_sorted( std::as_const( bpt ) ) );
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
    
        EXPECT_TRUE( std::ranges::is_sorted( bpt ) );
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
