////////////////////////////////////////////////////////////////////////////////
///
/// \file cow.cpp
/// -------------
///
/// Copy-on-write (COW) unit tests for vm_vector and b+tree.
///
/// Tests both memory-backed (anonymous) and file-backed COW cloning,
/// independent mutation of clones, and selective dirty-page commit.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include <psi/vm/containers/b+tree.hpp>
#include <psi/vm/containers/vm_vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <ranges>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// Helper: check if a value exists in a b+tree (workaround for contains() not
// being available at the bp_tree level).
template <typename Tree>
bool has( Tree const & tree, auto const & key ) { return tree.find( key ) != tree.end(); }

////////////////////////////////////////////////////////////////////////////////
// vm_vector COW tests
////////////////////////////////////////////////////////////////////////////////

TEST( vm_vector_cow, anon_memory_basic_clone )
{
    vm_vector<double, std::uint32_t> src;
    src.map_memory();
    src.append_range({ 1.0, 2.0, 3.0, 4.0, 5.0 });
    ASSERT_EQ( src.size(), 5 );

    auto clone{ src };

    // Clone has identical content
    ASSERT_EQ( clone.size(), src.size() );
    for ( std::uint32_t i{ 0 }; i < src.size(); ++i )
        EXPECT_EQ( clone[ i ], src[ i ] );
}

TEST( vm_vector_cow, anon_memory_independent_mutation )
{
    vm_vector<std::uint32_t, std::uint32_t> src;
    src.map_memory();

    // Fill with enough data to span multiple pages
    auto constexpr count{ 4096u }; // 4096 * 4 = 16KB, multiple pages
    for ( std::uint32_t i{ 0 }; i < count; ++i )
        src.push_back( i );
    ASSERT_EQ( src.size(), count );

    auto clone{ src };
    ASSERT_EQ( clone.size(), count );

    // Mutate the clone: modify a few elements
    clone[ 0 ]          = 0xDEAD;
    clone[ count / 2 ]  = 0xBEEF;
    clone[ count - 1 ]  = 0xCAFE;

    // Source must be unaffected
    EXPECT_EQ( src[ 0 ]         , 0u          );
    EXPECT_EQ( src[ count / 2 ] , count / 2   );
    EXPECT_EQ( src[ count - 1 ] , count - 1   );

    // Clone has the new values
    EXPECT_EQ( clone[ 0 ]         , 0xDEADu );
    EXPECT_EQ( clone[ count / 2 ] , 0xBEEFu );
    EXPECT_EQ( clone[ count - 1 ] , 0xCAFEu );
}

TEST( vm_vector_cow, anon_memory_source_mutation )
{
    // Note: for anonymous (memory-backed) COW on Windows, the clone gets a
    // PAGE_WRITECOPY view of the same section while the source retains
    // PAGE_READWRITE. Source writes go directly to the shared section and
    // ARE visible to the clone (until the clone itself writes to that page,
    // triggering a private copy).
    // True bidirectional isolation only exists for file-backed mappings
    // (both sides MAP_PRIVATE / WRITECOPY) or on macOS anonymous
    // (mach_vm_remap copy=TRUE).
    //
    // This test verifies the actual semantics: clone writes are isolated
    // from the source.
    vm_vector<std::uint32_t, std::uint32_t> src;
    src.map_memory();
    src.append_range({ 10u, 20u, 30u });

    auto clone{ src };

    // Clone writes are isolated from the source
    clone[ 0 ] = 42u;
    EXPECT_EQ( src[ 0 ], 10u ); // source unaffected
    EXPECT_EQ( clone[ 0 ], 42u );
}

TEST( vm_vector_cow, file_backed_basic_clone )
{
    auto const test_vec{ "test_cow.vec" };
    {
        vm_vector<double, std::uint32_t> src;
        src.map_file( test_vec, flags::named_object_construction_policy::create_new_or_truncate_existing );
        src.append_range({ 3.14, 2.72, 1.41, 1.73 });
        ASSERT_EQ( src.size(), 4 );

        auto clone{ src };
        ASSERT_EQ( clone.size(), 4 );
        EXPECT_EQ( clone[ 0 ], 3.14 );
        EXPECT_EQ( clone[ 1 ], 2.72 );
        EXPECT_EQ( clone[ 2 ], 1.41 );
        EXPECT_EQ( clone[ 3 ], 1.73 );

        // Mutate clone -- source (and file) unaffected
        clone[ 0 ] = 0.0;
        EXPECT_EQ( src[ 0 ], 3.14 );
    }
    // Verify the original file is intact
    {
        vm_vector<double, std::uint32_t> reopened;
        reopened.map_file( test_vec, flags::named_object_construction_policy::open_existing );
        EXPECT_EQ( reopened.size(), 4 );
        EXPECT_EQ( reopened[ 0 ], 3.14 );
    }
}

TEST( vm_vector_cow, empty_clone )
{
    vm_vector<int, std::uint32_t> src;
    src.map_memory();
    ASSERT_EQ( src.size(), 0 );

    auto clone{ src };
    EXPECT_EQ( clone.size(), 0 );
}

////////////////////////////////////////////////////////////////////////////////
// b+tree COW tests
////////////////////////////////////////////////////////////////////////////////

TEST( bptree_cow, anon_memory_basic_clone )
{
    bptree_set<int> src;
    src.map_memory();

    std::vector<int> values( 1000 );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );
    ASSERT_EQ( src.size(), 1000u );

    bptree_set<int> clone{ src };

    // Clone has identical content
    ASSERT_EQ( clone.size(), src.size() );
    EXPECT_TRUE( std::ranges::equal( src, clone ) );
}

TEST( bptree_cow, anon_memory_independent_mutation )
{
    bptree_set<int> src;
    src.map_memory();

    auto constexpr N{ 5000 };
    std::vector<int> values( N );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );

    bptree_set<int> clone{ src };

    // Insert into clone
    EXPECT_TRUE( clone.insert( N + 0 ).second );
    EXPECT_TRUE( clone.insert( N + 1 ).second );
    EXPECT_TRUE( clone.insert( N + 2 ).second );

    // Erase from clone
    EXPECT_TRUE( clone.erase( 0 ) );
    EXPECT_TRUE( clone.erase( 1 ) );

    // Source is unmodified
    EXPECT_EQ( src.size(), static_cast<std::size_t>( N ) );
    EXPECT_TRUE ( has( src, 0 ) );
    EXPECT_TRUE ( has( src, 1 ) );
    EXPECT_FALSE( has( src, N ) );

    // Clone has the modifications
    EXPECT_EQ( clone.size(), static_cast<std::size_t>( N + 3 - 2 ) );
    EXPECT_FALSE( has( clone, 0 ) );
    EXPECT_FALSE( has( clone, 1 ) );
    EXPECT_TRUE ( has( clone, 2 ) );
    EXPECT_TRUE ( has( clone, N + 0 ) );
    EXPECT_TRUE ( has( clone, N + 1 ) );
    EXPECT_TRUE ( has( clone, N + 2 ) );
}

TEST( bptree_cow, anon_memory_source_mutation )
{
    // Same semantics note as the vm_vector source_mutation test:
    // For anonymous COW on Windows the source retains a read-write view
    // so source writes propagate to the clone's shared pages. Only clone
    // writes are isolated (via WRITECOPY).
    // This test verifies clone-write isolation.
    bptree_set<int> src;
    src.map_memory();

    std::vector<int> values( 500 );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );

    bptree_set<int> clone{ src };

    // Clone writes are isolated from the source
    EXPECT_TRUE( clone.insert( 9999 ).second );
    EXPECT_TRUE( clone.erase( 42 ) );

    // Source retains original state
    EXPECT_EQ( src.size(), 500u );
    EXPECT_TRUE ( has( src, 42   ) );
    EXPECT_FALSE( has( src, 9999 ) );

    // Clone has the mutations
    EXPECT_EQ( clone.size(), 500u ); // erased one, inserted one
    EXPECT_FALSE( has( clone, 42   ) );
    EXPECT_TRUE ( has( clone, 9999 ) );
}

TEST( bptree_cow, anon_memory_sorted_order_preserved )
{
    bptree_set<int> src;
    src.map_memory();

    auto constexpr N{ 2000 };
    std::vector<int> values( N );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );

    bptree_set<int> clone{ src };

    // Insert some values that interleave with existing ones
    for ( int i{ 0 }; i < 100; ++i )
        clone.insert( N + i );

    // Erase some from the middle
    for ( int i{ 100 }; i < 200; ++i )
        (void)clone.erase( i );

#if !__SANITIZE_ADDRESS__
    EXPECT_TRUE( std::ranges::is_sorted( clone, clone.comp() ) );
#endif

    // Verify source is still perfectly sorted
#if !__SANITIZE_ADDRESS__
    EXPECT_TRUE( std::ranges::is_sorted( src, src.comp() ) );
#endif
    EXPECT_TRUE( std::ranges::equal( src, std::ranges::iota_view{ 0, N } ) );
}

TEST( bptree_cow, file_backed_basic_clone )
{
    auto const test_bpt{ "test_cow.bpt" };

    bptree_set<int> src;
    src.map_file( test_bpt, flags::named_object_construction_policy::create_new_or_truncate_existing );

    auto constexpr N{ 3000 };
    std::vector<int> values( N );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );
    ASSERT_EQ( src.size(), static_cast<std::size_t>( N ) );

    bptree_set<int> clone{ src };

    // Clone is readable and correct
    ASSERT_EQ( clone.size(), src.size() );
    EXPECT_TRUE( std::ranges::equal( src, clone ) );

    // Mutate clone -- source unaffected
    clone.insert( N );
    (void)clone.erase( 0 );

    EXPECT_EQ( src.size(), static_cast<std::size_t>( N ) );
    EXPECT_TRUE ( has( src, 0 ) );
    EXPECT_FALSE( has( src, N ) );
}

TEST( bptree_cow, file_backed_clone_does_not_corrupt_file )
{
    auto const test_bpt{ "test_cow_persist.bpt" };
    auto constexpr N{ 1000 };

    // Create and populate the file-backed tree
    {
        bptree_set<int> src;
        src.map_file( test_bpt, flags::named_object_construction_policy::create_new_or_truncate_existing );
        std::vector<int> values( N );
        std::iota( values.begin(), values.end(), 0 );
        src.insert( values );

        // Create a COW clone and heavily mutate it
        bptree_set<int> clone{ src };
        for ( int i{ 0 }; i < N / 2; ++i )
            (void)clone.erase( i * 2 ); // erase all even values
        for ( int i{ 0 }; i < 500; ++i )
            clone.insert( N + i );

        // Both go out of scope -- clone's changes must NOT leak to the file
    }

    // Reopen and verify original data
    {
        bptree_set<int> reopened;
        reopened.map_file( test_bpt, flags::named_object_construction_policy::open_existing );
        EXPECT_EQ( reopened.size(), static_cast<std::size_t>( N ) );
        EXPECT_TRUE( std::ranges::equal( reopened, std::ranges::iota_view{ 0, N } ) );
    }
}

TEST( bptree_cow, commit_to_memory_backed )
{
    bptree_set<int> src;
    src.map_memory();

    auto constexpr N{ 2000 };
    std::vector<int> values( N );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );

    // COW clone and mutate
    bptree_set<int> clone{ src };
    (void)clone.erase( 0 );
    (void)clone.erase( 1 );
    clone.insert( N );
    clone.insert( N + 1 );

    auto const expected_size{ clone.size() };
    clone.commit_to( src );

    // Source now reflects the clone's state
    EXPECT_EQ( src.size(), expected_size );
    EXPECT_FALSE( has( src, 0 ) );
    EXPECT_FALSE( has( src, 1 ) );
    EXPECT_TRUE ( has( src, N     ) );
    EXPECT_TRUE ( has( src, N + 1 ) );
}

TEST( bptree_cow, commit_clone_grows_beyond_source )
{
    // Verify correctness when the clone's node pool grows past the source's
    // initial mapped size. commit_to() pre-grows the target's node pool to
    // make room, then copies all dirty nodes (including the newly allocated ones).
    bptree_set<int> src;
    src.map_memory();

    auto constexpr N{ 50 };
    for ( int i{ 0 }; i < N; ++i )
        src.insert( i );

    // Clone and insert many more elements to force node-pool growth
    bptree_set<int> clone{ src };
    auto constexpr M{ 5000 };
    for ( int i{ N }; i < M; ++i )
        clone.insert( i );

    auto const expected_size{ clone.size() };
    EXPECT_EQ( expected_size, static_cast<std::size_t>( M ) );

    clone.commit_to( src );

    // src must have all M elements including those from the growth region
    EXPECT_EQ( src.size(), expected_size );
    EXPECT_TRUE( has( src, 0 ) );
    EXPECT_TRUE( has( src, N - 1 ) );
    EXPECT_TRUE( has( src, N     ) ); // first element from growth area
    EXPECT_TRUE( has( src, M - 1 ) ); // last element from growth area
    EXPECT_FALSE( src.empty() );
}

TEST( bptree_cow, commit_to_file_backed )
{
    auto const test_bpt{ "test_cow_commit.bpt" };
    auto constexpr N{ 1500 };

    // Create and populate a file-backed tree
    {
        bptree_set<int> src;
        src.map_file( test_bpt, flags::named_object_construction_policy::create_new_or_truncate_existing );
        std::vector<int> values( N );
        std::iota( values.begin(), values.end(), 0 );
        src.insert( values );

        // COW clone, mutate, commit back
        bptree_set<int> clone{ src };
        for ( int i{ 0 }; i < 100; ++i )
            (void)clone.erase( i ); // erase [0..99]
        for ( int i{ N }; i < N + 50; ++i )
            clone.insert( i ); // add [N..N+49]

        clone.commit_to( src );

        // Source now has the clone's state
        EXPECT_EQ( src.size(), static_cast<std::size_t>( N - 100 + 50 ) );
        EXPECT_FALSE( has( src, 0   ) );
        EXPECT_TRUE ( has( src, 100 ) );
        EXPECT_TRUE ( has( src, N   ) );
    }

    // Reopen and verify committed changes persisted
    {
        bptree_set<int> reopened;
        reopened.map_file( test_bpt, flags::named_object_construction_policy::open_existing );
        EXPECT_EQ( reopened.size(), static_cast<std::size_t>( N - 100 + 50 ) );
        EXPECT_FALSE( has( reopened, 0      ) );
        EXPECT_FALSE( has( reopened, 99     ) );
        EXPECT_TRUE ( has( reopened, 100    ) );
        EXPECT_TRUE ( has( reopened, N - 1  ) );
        EXPECT_TRUE ( has( reopened, N      ) );
        EXPECT_TRUE ( has( reopened, N + 49 ) );
    }
}

TEST( bptree_cow, empty_tree_clone )
{
    bptree_set<int> src;
    src.map_memory();
    ASSERT_TRUE( src.empty() );

    bptree_set<int> clone{ src };
    EXPECT_TRUE( clone.empty() );
    EXPECT_EQ( clone.size(), 0u );
}

TEST( bptree_cow, large_tree_clone )
{
    bptree_set<int> src;
    src.map_memory();

    // Large enough to span many nodes and tree levels
    auto constexpr N{ 100'000 };
    std::vector<int> values( N );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );

    bptree_set<int> clone{ src };
    ASSERT_EQ( clone.size(), static_cast<std::size_t>( N ) );

    // Spot-check a few values via find
    EXPECT_TRUE ( has( clone, 0       ) );
    EXPECT_TRUE ( has( clone, N / 2   ) );
    EXPECT_TRUE ( has( clone, N - 1   ) );
    EXPECT_FALSE( has( clone, N       ) );

    // Verify iteration produces the same elements
    EXPECT_TRUE( std::ranges::equal( src, clone ) );
}

TEST( bptree_cow, multiset_clone )
{
    bptree_multiset<int> src;
    src.map_memory();

    // Insert duplicates
    for ( int i{ 0 }; i < 100; ++i )
    {
        src.insert( i );
        src.insert( i ); // duplicate
    }
    ASSERT_EQ( src.size(), 200u );

    bptree_multiset<int> clone{ src };
    ASSERT_EQ( clone.size(), 200u );
    EXPECT_TRUE( std::ranges::equal( src, clone ) );

    // Mutate clone
    clone.insert( 50 ); // third copy of 50
    EXPECT_EQ( clone.size(), 201u );
    EXPECT_EQ( src.size(), 200u );
}


////////////////////////////////////////////////////////////////////////////////
// b+tree COW commit: dirty tracking edge cases
//
// These tests verify selective dirty-page commit through the b+tree
// commit_to() interface, which uses node_header::dirty bits and memcmp.
////////////////////////////////////////////////////////////////////////////////

TEST( bptree_cow, commit_no_mutations )
{
    // Clone with no mutations -- commit should be a no-op (source unchanged).
    bptree_set<int> src;
    src.map_memory();

    auto constexpr N{ 1000 };
    std::vector<int> values( N );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );

    // Snapshot source state
    std::vector<int> before( src.begin(), src.end() );

    bptree_set<int> clone{ src };
    // No mutations on clone
    clone.commit_to( src );

    // Source must be identical to before
    EXPECT_EQ( src.size(), static_cast<std::size_t>( N ) );
    std::vector<int> after( src.begin(), src.end() );
    EXPECT_EQ( before, after );
}

TEST( bptree_cow, commit_selective_only_dirty_nodes )
{
    // Verify that a small mutation on a large tree only changes the affected
    // portion -- the rest of the source should remain untouched.
    bptree_set<int> src;
    src.map_memory();

    auto constexpr N{ 10'000 };
    std::vector<int> values( N );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );

    bptree_set<int> clone{ src };

    // Mutate only a single element (touches ~1-2 nodes in a large tree)
    clone.insert( N ); // new max element

    clone.commit_to( src );

    // Source must now contain the single new element
    EXPECT_EQ( src.size(), static_cast<std::size_t>( N + 1 ) );
    EXPECT_TRUE( has( src, N ) );

    // All original elements still present
    EXPECT_TRUE( has( src, 0 ) );
    EXPECT_TRUE( has( src, N / 2 ) );
    EXPECT_TRUE( has( src, N - 1 ) );

    // Verify sorted order
#if !__SANITIZE_ADDRESS__
    EXPECT_TRUE( std::ranges::is_sorted( src, src.comp() ) );
#endif
}

TEST( bptree_cow, commit_erase_only )
{
    // Single clone→erase→commit cycle: erase-only mutations stay within
    // the source's node capacity (freed nodes, no new allocations).
    bptree_set<int> src;
    src.map_memory();

    auto constexpr N{ 5000 };
    std::vector<int> values( N );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );

    bptree_set<int> clone{ src };

    // Erase a scattered set of values
    for ( int i{ 0 }; i < N; i += 3 )
        (void)clone.erase( i );

    auto const expected_size{ static_cast<std::size_t>( N - ( ( N + 2 ) / 3 ) ) };
    EXPECT_EQ( clone.size(), expected_size );

    clone.commit_to( src );

    EXPECT_EQ( src.size(), expected_size );
    EXPECT_FALSE( has( src, 0 ) );
    EXPECT_FALSE( has( src, 3 ) );
    EXPECT_TRUE ( has( src, 1 ) );
    EXPECT_TRUE ( has( src, 2 ) );
    EXPECT_TRUE ( has( src, N - 1 ) ); // last value untouched
#if !__SANITIZE_ADDRESS__
    EXPECT_TRUE( std::ranges::is_sorted( src, src.comp() ) );
#endif
}

TEST( bptree_cow, commit_heavy_erase )
{
    // Heavy erase touching many nodes across the tree.
    // Verifies dirty tracking with large-scale node modifications.
    bptree_set<int> src;
    src.map_memory();

    auto constexpr N{ 5000 };
    std::vector<int> values( N );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );

    bptree_set<int> clone{ src };

    // Erase all even numbers (half the tree, touching every leaf node)
    for ( int i{ 0 }; i < N; i += 2 )
        (void)clone.erase( i );

    EXPECT_EQ( clone.size(), static_cast<std::size_t>( N / 2 ) );

    clone.commit_to( src );

    EXPECT_EQ( src.size(), static_cast<std::size_t>( N / 2 ) );
    EXPECT_FALSE( has( src, 0 ) );
    EXPECT_FALSE( has( src, 2 ) );
    EXPECT_FALSE( has( src, N - 2 ) );
    EXPECT_TRUE ( has( src, 1 ) );
    EXPECT_TRUE ( has( src, 3 ) );
    EXPECT_TRUE ( has( src, N - 1 ) );
#if !__SANITIZE_ADDRESS__
    EXPECT_TRUE( std::ranges::is_sorted( src, src.comp() ) );
#endif
}

TEST( bptree_cow, commit_clone_shrinks_below_source )
{
    // Clone shrinks significantly (lots of erases). commit_to must handle
    // the case where the clone's size is much smaller than the source.
    bptree_set<int> src;
    src.map_memory();

    auto constexpr N{ 5000 };
    std::vector<int> values( N );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );

    bptree_set<int> clone{ src };

    // Erase 80% of elements
    for ( int i{ 0 }; i < N; i += 5 )
    {
        (void)clone.erase( i );
        (void)clone.erase( i + 1 );
        (void)clone.erase( i + 2 );
        (void)clone.erase( i + 3 );
        // keep i+4
    }
    auto const clone_size{ clone.size() };
    EXPECT_EQ( clone_size, static_cast<std::size_t>( N / 5 ) );

    clone.commit_to( src );

    // Source now reflects the clone's heavily pruned state.
    // (After swap-based commit_to, we only verify size — clone holds old src state.)
    EXPECT_EQ( src.size(), clone_size );
}

TEST( bptree_cow, commit_scattered_erase )
{
    // Erase elements scattered throughout the tree (every 7th element).
    // This touches many different leaf nodes -- verifies dirty tracking
    // catches modifications across the entire tree.
    bptree_set<int> src;
    src.map_memory();

    auto constexpr N{ 7000 };
    std::vector<int> values( N );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );

    bptree_set<int> clone{ src };

    auto erased_count{ 0 };
    for ( int i{ 0 }; i < N; i += 7 )
    {
        (void)clone.erase( i );
        ++erased_count;
    }

    auto const expected_size{ static_cast<std::size_t>( N - erased_count ) };
    EXPECT_EQ( clone.size(), expected_size );

    clone.commit_to( src );

    EXPECT_EQ( src.size(), expected_size );
    EXPECT_FALSE( has( src, 0 ) );
    EXPECT_FALSE( has( src, 7 ) );
    EXPECT_FALSE( has( src, 6993 ) ); // last multiple of 7 < N
    EXPECT_TRUE ( has( src, 1 ) );
    EXPECT_TRUE ( has( src, 6 ) );
    EXPECT_TRUE ( has( src, 6999 ) ); // 6999 % 7 == 6 → not erased
#if !__SANITIZE_ADDRESS__
    EXPECT_TRUE( std::ranges::is_sorted( src, src.comp() ) );
#endif
}

TEST( bptree_cow, commit_to_file_backed_erase )
{
    // File-backed commit with erases: verify persistence to disk.
    auto const test_bpt{ "test_cow_file_erase.bpt" };
    auto constexpr N{ 2000 };

    // Create file-backed tree, clone→erase→commit, verify file persistence.
    {
        bptree_set<int> src;
        src.map_file( test_bpt, flags::named_object_construction_policy::create_new_or_truncate_existing );
        std::vector<int> values( N );
        std::iota( values.begin(), values.end(), 0 );
        src.insert( values );

        // Clone, erase first 400 elements, commit back
        {
            bptree_set<int> clone{ src };
            for ( int i{ 0 }; i < 400; ++i )
                (void)clone.erase( i );
            clone.commit_to( src );
        }

        EXPECT_EQ( src.size(), static_cast<std::size_t>( N - 400 ) );
        EXPECT_FALSE( has( src, 0 ) );
        EXPECT_FALSE( has( src, 399 ) );
        EXPECT_TRUE ( has( src, 400 ) );
        EXPECT_TRUE ( has( src, N - 1 ) );
    }

    // Reopen and verify final state persisted to file
    {
        bptree_set<int> reopened;
        reopened.map_file( test_bpt, flags::named_object_construction_policy::open_existing );
        EXPECT_EQ( reopened.size(), static_cast<std::size_t>( N - 400 ) );
        EXPECT_FALSE( has( reopened, 0   ) );
        EXPECT_FALSE( has( reopened, 399 ) );
        EXPECT_TRUE ( has( reopened, 400 ) );
        EXPECT_TRUE ( has( reopened, N - 1 ) );
#if !__SANITIZE_ADDRESS__
        EXPECT_TRUE( std::ranges::is_sorted( reopened, reopened.comp() ) );
#endif
    }
}

TEST( bptree_cow, commit_single_element_change )
{
    // Minimal commit: change exactly one element. Verifies that the dirty
    // tracking identifies the single modified leaf node.
    bptree_set<int> src;
    src.map_memory();

    auto constexpr N{ 10000 };
    std::vector<int> values( N );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );

    bptree_set<int> clone{ src };

    // Erase one element from the middle
    auto const target{ N / 2 };
    (void)clone.erase( target );
    EXPECT_EQ( clone.size(), static_cast<std::size_t>( N - 1 ) );

    clone.commit_to( src );

    EXPECT_EQ( src.size(), static_cast<std::size_t>( N - 1 ) );
    EXPECT_FALSE( has( src, target ) );
    // Neighbors are untouched
    EXPECT_TRUE( has( src, target - 1 ) );
    EXPECT_TRUE( has( src, target + 1 ) );
    EXPECT_TRUE( has( src, 0 ) );
    EXPECT_TRUE( has( src, N - 1 ) );
#if !__SANITIZE_ADDRESS__
    EXPECT_TRUE( std::ranges::is_sorted( src, src.comp() ) );
#endif
}

TEST( bptree_cow, commit_then_continue_using_source )
{
    // After committing to source, source should be fully functional for
    // further inserts/erases/iteration.
    bptree_set<int> src;
    src.map_memory();

    auto constexpr N{ 1500 };
    std::vector<int> values( N );
    std::iota( values.begin(), values.end(), 0 );
    src.insert( values );

    // Clone, mutate, commit
    {
        bptree_set<int> clone{ src };
        for ( int i{ 0 }; i < 100; ++i )
            (void)clone.erase( i );
        clone.insert( N );
        clone.commit_to( src );
    }

    // Source is now modified; verify it's fully usable
    EXPECT_EQ( src.size(), static_cast<std::size_t>( N - 100 + 1 ) );

    // Continue mutating source directly
    src.insert( N + 1 );
    src.insert( N + 2 );
    (void)src.erase( 200 );

    EXPECT_EQ( src.size(), static_cast<std::size_t>( N - 100 + 1 + 2 - 1 ) );
    EXPECT_TRUE ( has( src, N + 1 ) );
    EXPECT_TRUE ( has( src, N + 2 ) );
    EXPECT_FALSE( has( src, 200 ) );

#if !__SANITIZE_ADDRESS__
    EXPECT_TRUE( std::ranges::is_sorted( src, src.comp() ) );
#endif

    // Clone again from the mutated source -- verify it works
    bptree_set<int> clone2{ src };
    EXPECT_EQ( clone2.size(), src.size() );
    EXPECT_TRUE( std::ranges::equal( src, clone2 ) );
}

TEST( bptree_cow, commit_multiset_clone )
{
    // multiset_clone: bptree_multiset COW (allows duplicate keys).
    // Verifies that duplicate keys are correctly handled by COW + commit.
    bptree_multiset<int> src;
    src.map_memory();

    // Insert values with duplicates
    for ( int i{ 0 }; i < 500; ++i )
    {
        src.insert( i );
        src.insert( i ); // duplicate
    }
    ASSERT_EQ( src.size(), 1000u );

    bptree_multiset<int> clone{ src };
    ASSERT_EQ( clone.size(), 1000u );

    // Add more duplicates to clone
    for ( int i{ 0 }; i < 100; ++i )
        clone.insert( i ); // third copy of 0..99

    // Erase one copy of 499
    auto it{ clone.find( 499 ) };
    ASSERT_NE( it, clone.end() );
    clone.erase( it );

    EXPECT_EQ( clone.size(), 1099u ); // +100 -1

    clone.commit_to( src );

    EXPECT_EQ( src.size(), 1099u );
    EXPECT_EQ( std::ranges::count( src, 50 ), 3 );   // 3 copies of 50
    EXPECT_EQ( std::ranges::count( src, 499 ), 1 );   // 1 copy left
    EXPECT_EQ( std::ranges::count( src, 250 ), 2 );   // untouched duplicates
    EXPECT_TRUE( std::ranges::is_sorted( src, src.comp() ) );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
