////////////////////////////////////////////////////////////////////////////////
///
/// Node-fill, space and throughput characterisation for bp_tree.
///
/// Reports, per insertion pattern, the achieved leaf fill factor and the
/// resulting bytes-per-key - the numbers that decide whether a different
/// overflow/split policy is worth its cost.  Fill is measured through the
/// public leaf range so it stays valid for any internal layout.
///
////////////////////////////////////////////////////////////////////////////////

#include <psi/vm/containers/b+tree.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <numeric>
#include <optional>
#include <print>
#include <random>
#include <ranges>
#include <span>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

#ifdef NDEBUG // meaningful only in optimized builds

namespace
{
    using timer = std::chrono::steady_clock;

    struct fill_stats
    {
        std::uint64_t leaves      { 0 };
        std::uint64_t keys        { 0 };
        std::uint64_t leaf_bytes  { 0 };
        std::uint64_t nodes_used  { 0 }; // every level, not just leaves
        std::uint64_t pool_bytes  { 0 }; // what the node pool actually holds
        double        fill        { 0 }; // [0,1] of leaf key capacity
        double        bytes_p_key { 0 }; // leaf bytes only (inner levels are ~1.6% on top)
        double        pool_p_key  { 0 }; // resident bytes per key: the whole pool, reserve slack included
    };

    // Walks every leaf: counts fill and, at the same time, checks that the leaf
    // chain is globally sorted and accounts for exactly size() keys.  A fill
    // number is only worth reading if the tree it came from is still a tree.
    template <typename Tree>
    fill_stats measure_fill( Tree const & tree )
    {
        fill_stats stats;
        std::optional<std::uint32_t> previous_key;
        for ( auto const leaf_keys : tree.leaves() )
        {
            EXPECT_FALSE( leaf_keys.empty() );
            EXPECT_TRUE( std::ranges::is_sorted( leaf_keys ) );
            if ( previous_key && !leaf_keys.empty() ) {
                EXPECT_LT( *previous_key, leaf_keys.front() ) << "leaf chain out of order";
            }
            if ( !leaf_keys.empty() ) previous_key = leaf_keys.back();
            ++stats.leaves;
            stats.keys += leaf_keys.size();
        }
        EXPECT_EQ( stats.keys, tree.size() ) << "leaf chain does not account for every key";
        auto constexpr leaf_size    { sizeof( typename Tree::leaf_node ) };
        auto constexpr leaf_capacity{ Tree::leaf_node::max_values };
        stats.leaf_bytes  = stats.leaves * leaf_size;
        stats.nodes_used  = tree.nodes_used();
        stats.pool_bytes  = std::uint64_t( tree.nodes_reserved() ) * Tree::node_byte_size();
        stats.pool_p_key  = stats.keys ? double( stats.pool_bytes ) / double( stats.keys ) : 0;
        stats.fill        = stats.leaves ? double( stats.keys ) / double( stats.leaves * leaf_capacity ) : 0;
        stats.bytes_p_key = stats.keys   ? double( stats.leaf_bytes ) / double( stats.keys )             : 0;
        return stats;
    }

    void report( std::string_view const pattern, std::uint64_t const n, fill_stats const & s, timer::duration const elapsed )
    {
        std::println
        (
            "{:<22} n={:>9}  leaves={:>8}  nodes={:>8}  fill={:>6.2f}%  leafB/key={:>6.2f}  poolB/key={:>6.2f}  {:>8.1f} ns/key",
            pattern, n, s.leaves, s.nodes_used, s.fill * 100, s.bytes_p_key, s.pool_p_key,
            double( std::chrono::duration_cast<std::chrono::nanoseconds>( elapsed ).count() ) / double( n )
        );
    }

    std::vector<std::uint32_t> shuffled( std::uint32_t const n, std::uint32_t const seed )
    {
        auto data{ std::ranges::to<std::vector>( std::views::iota( std::uint32_t{ 0 }, n ) ) };
        std::mt19937 rng{ seed };
        std::ranges::shuffle( data, rng );
        return data;
    }

    using tree_t = bptree_set<std::uint32_t>;

    // The hinted overload requires a dereferenceable hint that compares greater
    // than the inserted key, so a non-empty tree and a hint that is not end().
    void hinted_insert( tree_t & tree, std::uint32_t const key )
    {
        if ( tree.empty() ) { tree.insert( key ); return; }
        auto const hint{ tree.lower_bound( key ) };
        if ( hint == tree.end() ) { tree.insert( key ); return; }
        tree.insert( hint, key );
    }

    tree_t make_tree()
    {
        tree_t tree;
        BOOST_VERIFY( tree.map_memory()().succeeded() );
        return tree;
    }
} // anonymous namespace

TEST( bp_tree, node_fill_characterisation )
{
    std::println( "leaf node: {} bytes, {} keys capacity", sizeof( tree_t::leaf_node ), tree_t::leaf_node::max_values );
    std::println( "inner node: {} children, {} keys capacity", tree_t::inner_node::max_children, tree_t::inner_node::max_values );

    for ( std::uint32_t const n : { 100'000U, 1'000'000U, 8'000'000U } )
    {
        auto const random_keys{ shuffled( n, 42 ) };

        { // one-by-one, random order - the pattern an overflow policy actually governs
            auto tree{ make_tree() };
            auto const start{ timer::now() };
            for ( auto const key : random_keys )
                tree.insert( key );
            auto const elapsed{ timer::now() - start };
            EXPECT_EQ( tree.size(), n );
            report( "single/random", n, measure_fill( tree ), elapsed );
        }

        { // one-by-one, ascending - the classic append pattern
            auto tree{ make_tree() };
            auto const start{ timer::now() };
            for ( std::uint32_t key{ 0 }; key < n; ++key )
                tree.insert( key );
            auto const elapsed{ timer::now() - start };
            EXPECT_EQ( tree.size(), n );
            report( "single/ascending", n, measure_fill( tree ), elapsed );
        }

        { // one-by-one, descending
            auto tree{ make_tree() };
            auto const start{ timer::now() };
            for ( std::uint32_t key{ n }; key-- > 0; )
                tree.insert( key );
            auto const elapsed{ timer::now() - start };
            EXPECT_EQ( tree.size(), n );
            report( "single/descending", n, measure_fill( tree ), elapsed );
        }

        { // bulk, unsorted input
            auto tree{ make_tree() };
            auto const start{ timer::now() };
            tree.insert( random_keys.begin(), random_keys.end() );
            auto const elapsed{ timer::now() - start };
            EXPECT_EQ( tree.size(), n );
            report( "bulk/unsorted", n, measure_fill( tree ), elapsed );
        }

        { // bulk, presorted input
            auto sorted_keys{ random_keys };
            std::ranges::sort( sorted_keys );
            auto tree{ make_tree() };
            auto const start{ timer::now() };
            tree.insert_presorted_unique( sorted_keys );
            auto const elapsed{ timer::now() - start };
            EXPECT_EQ( tree.size(), n );
            report( "bulk/presorted", n, measure_fill( tree ), elapsed );
        }

        { // one-by-one with a lower_bound hint - the shape a sorted-index maintainer uses
            auto tree{ make_tree() };
            auto const start{ timer::now() };
            for ( auto const key : random_keys )
                hinted_insert( tree, key );
            auto const elapsed{ timer::now() - start };
            EXPECT_EQ( tree.size(), n );
            report( "hinted/random", n, measure_fill( tree ), elapsed );
        }

        { // ascending with a hint - append through the hinted path
            auto tree{ make_tree() };
            auto const start{ timer::now() };
            for ( std::uint32_t key{ 0 }; key < n; ++key )
                hinted_insert( tree, key );
            auto const elapsed{ timer::now() - start };
            EXPECT_EQ( tree.size(), n );
            report( "hinted/ascending", n, measure_fill( tree ), elapsed );
        }

        { // repeated bulk merges of sorted batches whose keys interleave with the
          // existing ones - the shape a transactional index maintainer produces,
          // and the only pattern that reaches the split path through merge()
            auto tree{ make_tree() };
            auto const batch_count{ 64U };
            auto const batch_size { n / batch_count };
            auto const start{ timer::now() };
            for ( std::uint32_t b{ 0 }; b < batch_count; ++b )
            {
                std::vector<std::uint32_t> batch;
                batch.reserve( batch_size );
                for ( std::uint32_t i{ 0 }; i < batch_size; ++i )
                    batch.push_back( random_keys[ b * batch_size + i ] );
                std::ranges::sort( batch );
                tree.insert( batch.begin(), batch.end() );
            }
            auto const elapsed{ timer::now() - start };
            EXPECT_EQ( tree.size(), batch_count * batch_size );
            report( "merge/interleaved", n, measure_fill( tree ), elapsed );
        }

        { // the same, but every batch lands past the current maximum - the
          // append lane, which takes the bulk-append path instead of splitting
            auto tree{ make_tree() };
            auto const batch_count{ 64U };
            auto const batch_size { n / batch_count };
            auto const start{ timer::now() };
            for ( std::uint32_t b{ 0 }; b < batch_count; ++b )
            {
                std::vector<std::uint32_t> batch;
                batch.reserve( batch_size );
                for ( std::uint32_t i{ 0 }; i < batch_size; ++i )
                    batch.push_back( b * batch_size + i );
                tree.insert( batch.begin(), batch.end() );
            }
            auto const elapsed{ timer::now() - start };
            EXPECT_EQ( tree.size(), batch_count * batch_size );
            report( "merge/appended", n, measure_fill( tree ), elapsed );
        }

        { // random build, then erase half at random - what erase leaves behind
            auto tree{ make_tree() };
            for ( auto const key : random_keys )
                tree.insert( key );
            auto const start{ timer::now() };
            for ( auto const key : std::span{ random_keys }.first( n / 2 ) )
                EXPECT_TRUE( tree.erase( key ) );
            auto const elapsed{ timer::now() - start };
            EXPECT_EQ( tree.size(), n - n / 2 );
            report( "random+erase half", n / 2, measure_fill( tree ), elapsed );
        }
        std::println( "" );
    }
}

TEST( bp_tree, lookup_throughput )
{
    for ( std::uint32_t const n : { 100'000U, 1'000'000U, 8'000'000U } )
    {
        auto const random_keys{ shuffled( n, 7 ) };
        auto tree{ make_tree() };
        tree.insert( random_keys.begin(), random_keys.end() );

        auto const probes{ shuffled( n, 13 ) };
        auto const start { timer::now() };
        std::uint64_t found{ 0 };
        for ( auto const key : probes )
            found += ( tree.find( key ) != tree.end() );
        auto const elapsed{ timer::now() - start };
        EXPECT_EQ( found, n );
        std::println
        (
            "{:<22} n={:>9}  {:>8.1f} ns/lookup",
            "find/random", n,
            double( std::chrono::duration_cast<std::chrono::nanoseconds>( elapsed ).count() ) / double( n )
        );
    }
}

#endif // NDEBUG

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
