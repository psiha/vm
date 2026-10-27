////////////////////////////////////////////////////////////////////////////////
/// psi::vm::attributed_span, sorted_range and sorted_view test suite
////////////////////////////////////////////////////////////////////////////////

#include <psi/vm/attributed_span.hpp>
#include <psi/vm/sorted_view.hpp>
#include <psi/vm/containers/heap_vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <compare>
#include <cstdint>
#include <numeric>
#include <random>
#include <span>
#include <tuple>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
namespace psi::vm {
//------------------------------------------------------------------------------

namespace
{
    constexpr span_facts sorted_facts       { .sorted = true };
    constexpr span_facts sorted_unique_facts{ .sorted = true, .unique = true };
    constexpr span_facts permutable_facts   { .mutability = span_mutability::permutable  };
    constexpr span_facts destroyable_facts  { .mutability = span_mutability::destroyable };

    template <typename T>
    std::vector<T> random_keys( std::size_t const count, std::uint32_t const modulo, std::uint32_t const seed )
    {
        std::mt19937 rng{ seed };
        std::vector<T> keys( count );
        for ( auto & key : keys )
            key = static_cast<T>( rng() % modulo );
        return keys;
    }

    template <typename T>
    std::vector<T> sorted_unique_of( std::vector<T> keys )
    {
        std::ranges::sort( keys );
        keys.erase( std::unique( keys.begin(), keys.end() ), keys.end() );
        return keys;
    }

    template <typename T>
    std::vector<T> sorted_of( std::vector<T> keys )
    {
        std::ranges::sort( keys );
        return keys;
    }

    template <typename Span>
    std::vector<std::remove_const_t<typename Span::element_type>> copy_of( Span const span )
    {
        return { span.begin(), span.end() };
    }
} // anonymous namespace


TEST( attributed_span, is_two_registers_and_converts_to_and_from_std_span )
{
    static_assert( sizeof( attributed_span<std::uint32_t> ) == 2 * sizeof( void * ) );
    static_assert( std::is_trivially_copyable_v<attributed_span<std::uint32_t>> );
    static_assert( std::is_convertible_v<attributed_span<std::uint32_t>, std::span<std::uint32_t>> );
    static_assert( std::is_convertible_v<attributed_span<std::uint32_t>, std::span<std::uint32_t const>> );
    static_assert( std::is_convertible_v<std::span<std::uint32_t>, attributed_span<std::uint32_t>> );
    static_assert( std::is_convertible_v<std::vector<std::uint32_t> &, attributed_span<std::uint32_t>> );
    static_assert( std::is_convertible_v<std::vector<std::uint32_t> const &, attributed_span<std::uint32_t const>> );
    static_assert( !std::is_convertible_v<std::vector<std::uint32_t> const &, attributed_span<std::uint32_t>> );
    static_assert( std::is_convertible_v<attributed_span<std::uint32_t>, attributed_span<std::uint32_t const>> );
    static_assert( !std::is_convertible_v<attributed_span<std::uint32_t const>, attributed_span<std::uint32_t>> );

    std::vector<std::uint32_t> values{ 1, 2, 3 };
    attributed_span const view{ values, span_facts{ .sorted = true, .unique = true, .mutability = span_mutability::permutable } };
    std::span<std::uint32_t> const plain{ view };
    EXPECT_EQ( plain.data(), values.data() );
    EXPECT_EQ( plain.size(), 3u );
    EXPECT_TRUE ( view.is_sorted() );
    EXPECT_TRUE ( view.is_unique() );
    EXPECT_TRUE ( view.may_permute() );
    EXPECT_FALSE( view.may_destroy() );
}

TEST( attributed_span, a_const_view_cannot_be_permuted_or_destroyed )
{
    std::vector<std::uint32_t> values{ 1, 2, 3 };
    attributed_span<std::uint32_t const> const view{ values, span_facts{ .sorted = true, .mutability = span_mutability::destroyable } };
    EXPECT_TRUE ( view.is_sorted() );
    EXPECT_FALSE( view.may_permute() );
    EXPECT_FALSE( view.may_destroy() );

    attributed_span<std::uint32_t> const mutable_view{ values, destroyable_facts };
    EXPECT_TRUE( mutable_view.may_permute() ) << "destroyable implies permutable";
    attributed_span<std::uint32_t const> const narrowed{ mutable_view };
    EXPECT_FALSE( narrowed.may_permute() );
}

TEST( attributed_span, only_a_permutable_span_gives_out_a_reorderable_span )
{
    std::vector<std::uint32_t> values{ 3, 1, 2 };
    attributed_span const permutable{ values, permutable_facts };
    EXPECT_EQ( permutable.permutable_span().data(), values.data() );
    attributed_span const destroyable{ values, destroyable_facts };
    EXPECT_EQ( destroyable.destroyable_span().size(), 3u );
    EXPECT_EQ( destroyable.permutable_span().size(), 3u ) << "destroyable implies permutable";
#ifndef NDEBUG
    attributed_span const immutable{ values };
    EXPECT_DEATH( std::ignore = immutable.permutable_span(), "" );
    EXPECT_DEATH( std::ignore = permutable.destroyable_span(), "" );
#endif
}

TEST( attributed_span, a_run_of_the_elements_keeps_the_facts )
{
    std::vector<std::uint32_t> values{ 1, 2, 3, 4, 5 };
    attributed_span const view{ values, span_facts{ .sorted = true, .unique = true, .mutability = span_mutability::destroyable } };
    for ( auto const run : { view.first( 2 ), view.last( 2 ), view.subspan( 1, 3 ), view.subspan( 3 ) } )
    {
        EXPECT_EQ( run.facts(), view.facts() );
    }
    EXPECT_EQ( view.subspan( 1, 3 ).front(), 2u );
    EXPECT_EQ( view.last( 2 ).back(), 5u );
    EXPECT_EQ( view.restricted( sorted_facts ).facts(), sorted_facts );
}

TEST( span_facts, combine_as_the_weakest_of_them )
{
    constexpr span_facts a{ .sorted = true, .unique = true , .mutability = span_mutability::destroyable };
    constexpr span_facts b{ .sorted = true, .unique = false, .mutability = span_mutability::permutable };
    static_assert( ( a & b ) == span_facts{ .sorted = true, .unique = false, .mutability = span_mutability::permutable } );
    static_assert( ( a & span_facts{} ) == span_facts{} );
    EXPECT_EQ( ( a & b ).mutability, span_mutability::permutable );
}

TEST( sorted_range, converts_to_an_attributed_span_with_its_facts )
{
    std::vector<std::uint32_t> const values{ 1, 2, 2, 5 };
    sorted_range const sorted{ values };
    attributed_span<std::uint32_t const> const view{ sorted };
    EXPECT_EQ( view.facts(), sorted_facts );
    EXPECT_EQ( view.data(), values.data() );

    std::vector<std::uint32_t> const distinct{ 1, 2, 5 };
    sorted_range<std::vector<std::uint32_t> const &, true> const unique{ distinct };
    attributed_span<std::uint32_t const> const unique_view{ unique };
    EXPECT_EQ( unique_view.facts(), sorted_unique_facts );
}

#ifndef NDEBUG
TEST( attributed_span, a_false_claim_is_caught_in_debug_builds )
{
    std::vector<std::uint32_t> unsorted{ 3, 1, 2 };
    EXPECT_DEATH( ( attributed_span<std::uint32_t>{ unsorted, sorted_facts } ), "" );
    std::vector<std::uint32_t> repeated{ 1, 1, 2 };
    EXPECT_DEATH( ( attributed_span<std::uint32_t>{ repeated, sorted_unique_facts } ), "" );
}
#endif


TEST( sorted_view, a_sorted_span_is_returned_as_it_is )
{
    std::vector<std::uint32_t> keys{ 1, 4, 4, 9 };
    std::vector<std::uint32_t> scratch;
    auto const view{ sorted_view( attributed_span{ keys, sorted_facts }, scratch ) };
    EXPECT_EQ( view.data(), keys.data() );
    EXPECT_TRUE( view.is_sorted() );
    EXPECT_TRUE( scratch.empty() );
}

TEST( sorted_view, a_permutable_span_is_sorted_in_place )
{
    auto keys{ random_keys<std::uint32_t>( 1000, 50, 1 ) };
    auto const expected{ sorted_of( keys ) };
    std::vector<std::uint32_t> scratch;
    auto const view{ sorted_view( attributed_span{ keys, permutable_facts }, scratch ) };
    EXPECT_EQ( view.data(), keys.data() );
    EXPECT_EQ( copy_of( view ), expected );
    EXPECT_TRUE( scratch.empty() );
}

TEST( sorted_view, a_span_that_must_be_kept_is_copied_before_it_is_sorted )
{
    auto const keys{ random_keys<std::uint32_t>( 1000, 50, 2 ) };
    auto const original{ keys };
    std::vector<std::uint32_t> scratch;
    auto const view{ sorted_view( attributed_span<std::uint32_t const>{ keys }, scratch ) };
    EXPECT_EQ( view.data(), scratch.data() );
    EXPECT_EQ( copy_of( view ), sorted_of( keys ) );
    EXPECT_EQ( keys, original );

    std::vector<std::uint32_t> writable{ original };
    auto const view2{ sorted_view( attributed_span{ writable }, scratch ) };
    EXPECT_EQ( copy_of( view2 ), sorted_of( original ) );
    EXPECT_EQ( writable, original );
}

TEST( sorted_view, unique_removes_the_repeated_keys_on_every_path )
{
    for ( std::size_t const count : { 0u, 1u, 2u, 7u, 100u, 5000u } )
    {
        auto const original{ random_keys<std::uint32_t>( count, 40, static_cast<std::uint32_t>( count ) ) };
        auto const expected{ sorted_unique_of( original ) };
        std::vector<std::uint32_t> scratch;

        // not sorted: copied / in place
        {
            auto const view{ sorted_view<key_sort_algo::pdq, true>( attributed_span<std::uint32_t const>{ original }, scratch ) };
            EXPECT_EQ( copy_of( view ), expected ) << count;
            EXPECT_TRUE( view.is_unique() );
        }
        {
            auto keys{ original };
            auto const view{ sorted_view<key_sort_algo::pdq, true>( attributed_span{ keys, permutable_facts }, scratch ) };
            EXPECT_EQ( copy_of( view ), expected ) << count;
        }
        // sorted, with repeats: copied / in place
        auto const sorted{ sorted_of( original ) };
        {
            auto const view{ sorted_view<key_sort_algo::pdq, true>( attributed_span<std::uint32_t const>{ sorted, sorted_facts }, scratch ) };
            EXPECT_EQ( copy_of( view ), expected ) << count;
        }
        {
            auto keys{ sorted };
            auto const view{ sorted_view<key_sort_algo::pdq, true>( attributed_span{ keys, span_facts{ .sorted = true, .mutability = span_mutability::permutable } }, scratch ) };
            EXPECT_EQ( copy_of( view ), expected ) << count;
        }
        // already sorted and unique: untouched
        {
            auto const view{ sorted_view<key_sort_algo::pdq, true>( attributed_span<std::uint32_t const>{ expected, sorted_unique_facts }, scratch ) };
            EXPECT_EQ( view.data(), expected.data() );
        }
    }
}

namespace
{
    // Orders as the unsigned integer it holds, with no operator== (a
    // user provided <=> does not synthesize one).
    struct ordered_key
    {
        static constexpr bool orders_as_unsigned{ true };

        std::uint32_t value;

        std::strong_ordering operator<=>( ordered_key const & other ) const noexcept { return value <=> other.value; }
    };
} // anonymous namespace

TEST( sorted_view, needs_no_equality_of_the_keys )
{
    static_assert( !std::equality_comparable<ordered_key> );

    std::vector<ordered_key> const sorted{ { 1 }, { 1 }, { 4 }, { 4 }, { 4 }, { 9 } };
    std::vector<ordered_key> const shuffled{ { 4 }, { 1 }, { 9 }, { 4 }, { 1 }, { 4 } };
    std::vector<ordered_key> scratch;
    auto const values_of{ []( auto const view ) { std::vector<std::uint32_t> values; for ( auto const key : view ) values.push_back( key.value ); return values; } };
    std::vector<std::uint32_t> const expected_sorted { 1, 1, 4, 4, 4, 9 };
    std::vector<std::uint32_t> const expected_unique { 1, 4, 9 };

    EXPECT_EQ( values_of( sorted_view( attributed_span<ordered_key const>{ shuffled }, scratch ) ), expected_sorted );
    EXPECT_EQ( values_of( sorted_view<key_sort_algo::pdq, true>( attributed_span<ordered_key const>{ shuffled }, scratch ) ), expected_unique );
    EXPECT_EQ( values_of( sorted_view<key_sort_algo::pdq, true>( attributed_span<ordered_key const>{ sorted, sorted_facts }, scratch ) ), expected_unique );
    auto writable{ sorted };
    EXPECT_EQ( values_of( sorted_view<key_sort_algo::pdq, true>( attributed_span{ writable, span_facts{ .sorted = true, .mutability = span_mutability::permutable } }, scratch ) ), expected_unique );
}

TEST( sorted_view, sorts_elements_that_are_not_keys )
{
    // Not trivially copyable and not an integer in disguise.
    std::vector<std::string> const shuffled{ "pear", "apple", "fig", "apple", "pear", "kiwi" };
    std::vector<std::string> const expected_sorted{ "apple", "apple", "fig", "kiwi", "pear", "pear" };
    std::vector<std::string> const expected_unique{ "apple", "fig", "kiwi", "pear" };
    std::vector<std::string> scratch;
    auto const strings_of{ []( auto const view ) { return std::vector<std::string>( view.begin(), view.end() ); } };

    EXPECT_EQ( strings_of( sorted_view( attributed_span<std::string const>{ shuffled }, scratch ) ), expected_sorted );
    EXPECT_EQ( strings_of( sorted_view<key_sort_algo::pdq, true>( attributed_span<std::string const>{ shuffled }, scratch ) ), expected_unique );
    auto writable{ shuffled };
    auto const in_place{ sorted_view<key_sort_algo::pdq, true>( attributed_span{ writable, permutable_facts }, scratch ) };
    EXPECT_EQ( in_place.data(), writable.data() );
    EXPECT_EQ( strings_of( in_place ), expected_unique );
    EXPECT_EQ( strings_of( sorted_view<key_sort_algo::pdq, true>( attributed_span<std::string const>{ expected_sorted, sorted_facts }, scratch ) ), expected_unique );
}

TEST( sorted_view, works_with_a_psi_vector_as_scratch )
{
    auto const keys{ random_keys<std::uint32_t>( 3000, 1000, 9 ) };
    heap_vector<std::uint32_t> scratch;
    auto const view{ sorted_view<key_sort_algo::pdq>( attributed_span<std::uint32_t const>{ keys }, scratch ) };
    EXPECT_EQ( copy_of( view ), sorted_of( keys ) );
}

namespace
{
    // Orders ids by a rank table: a comparator with state.
    struct by_rank
    {
        std::vector<int> const * rank;

        bool operator()( std::uint32_t const a, std::uint32_t const b ) const noexcept { return ( *rank )[ a ] < ( *rank )[ b ]; }
    };
} // anonymous namespace

TEST( attributed_span, the_sorted_fact_is_by_the_comparator_of_the_span )
{
    static_assert( !std::is_same_v<attributed_span<std::uint32_t, std::greater<>>, attributed_span<std::uint32_t>> );
    static_assert(  std::is_convertible_v<attributed_span<std::uint32_t, std::greater<>>, attributed_span<std::uint32_t>> );

    std::vector<std::uint32_t> descending{ 9, 5, 5, 1 };
    attributed_span<std::uint32_t, std::greater<>> const view{ descending, sorted_facts };
    EXPECT_TRUE( view.is_sorted() );

    // To another order only what does not depend on it is kept.
    attributed_span<std::uint32_t, std::greater<>> const permutable_descending{ descending, span_facts{ .sorted = true, .unique = false, .mutability = span_mutability::permutable } };
    attributed_span<std::uint32_t> const natural{ permutable_descending };
    EXPECT_EQ( natural.facts(), permutable_facts );
    EXPECT_EQ( natural.data(), descending.data() );
    attributed_span<std::uint32_t const, std::greater<>> const read_only{ attributed_span<std::uint32_t, std::less<>>{ descending, destroyable_facts } };
    EXPECT_EQ( read_only.facts(), span_facts{} );
#ifndef NDEBUG
    EXPECT_DEATH( ( attributed_span<std::uint32_t>{ descending, sorted_facts } ), "" );
    EXPECT_DEATH( ( attributed_span<std::uint32_t, std::greater<>>{ descending, sorted_unique_facts } ), "" );
#endif
}

TEST( attributed_span, a_comparator_with_state_has_to_be_passed_in_to_state_facts )
{
    using ranked_span = attributed_span<std::uint32_t, by_rank>;
    static_assert( !std::is_constructible_v<ranked_span, std::span<std::uint32_t>, span_facts> );
    static_assert(  std::is_constructible_v<ranked_span, std::span<std::uint32_t>, span_facts, by_rank> );
    static_assert( std::is_convertible_v<std::span<std::uint32_t>, ranked_span>, "no facts, nothing to verify" );

    std::vector<int> const rank{ 2, 0, 1 }; // the order of the ids is 1, 2, 0
    by_rank const comp{ &rank };
    std::vector<std::uint32_t> ids{ 1, 2, 0 };
    ranked_span const view{ ids, sorted_facts, comp };
    EXPECT_TRUE( view.is_sorted() );
    EXPECT_EQ( view.subspan( 1 ).facts(), sorted_facts ) << "a run of a sorted span is not verified again";
#ifndef NDEBUG
    std::vector<std::uint32_t> unsorted{ 0, 1, 2 };
    EXPECT_DEATH( ( ranked_span{ unsorted, sorted_facts, comp } ), "" );
#endif
}

TEST( sorted_view, sorts_by_the_comparator_of_the_span )
{
    std::vector<std::uint32_t> scratch;

    std::vector<std::uint32_t> const keys{ 3, 9, 1, 9, 5 };
    auto const descending{ sorted_view( attributed_span<std::uint32_t const, std::greater<>>{ keys }, scratch ) };
    EXPECT_EQ( copy_of( descending ), ( std::vector<std::uint32_t>{ 9, 9, 5, 3, 1 } ) );
    auto const descending_unique{ sorted_view<key_sort_algo::pdq, true>( attributed_span<std::uint32_t const, std::greater<>>{ keys }, scratch ) };
    EXPECT_EQ( copy_of( descending_unique ), ( std::vector<std::uint32_t>{ 9, 5, 3, 1 } ) );

    std::vector<int> const rank{ 2, 0, 1 }; // the order of the ids is 1, 2, 0
    by_rank const comp{ &rank };
    std::vector<std::uint32_t> const ids{ 0, 1, 2, 1, 0 };
    auto const ranked{ sorted_view( attributed_span<std::uint32_t const, by_rank>{ ids }, scratch, comp ) };
    EXPECT_EQ( copy_of( ranked ), ( std::vector<std::uint32_t>{ 1, 1, 2, 0, 0 } ) );
    auto const ranked_unique{ sorted_view<key_sort_algo::pdq, true>( attributed_span<std::uint32_t const, by_rank>{ ids }, scratch, comp ) };
    EXPECT_EQ( copy_of( ranked_unique ), ( std::vector<std::uint32_t>{ 1, 2, 0 } ) );

    std::vector<std::uint32_t> const ordered{ 1, 1, 2, 0 };
    auto const already{ sorted_view( attributed_span<std::uint32_t const, by_rank>{ ordered, sorted_facts, comp }, scratch, comp ) };
    EXPECT_EQ( already.data(), ordered.data() );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
