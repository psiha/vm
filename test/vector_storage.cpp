////////////////////////////////////////////////////////////////////////////////
///
/// \file vector_storage.cpp
/// ------------------------
///
/// Tests for the storage-parameterized vector<Storage> template and the
/// extracted storage classes (heap_storage, fixed_storage, vm_storage).
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include <psi/vm/containers/vector.hpp>
#include <psi/vm/containers/tr_vector.hpp>
#include <psi/vm/containers/fc_vector.hpp>
#include <psi/vm/containers/vm_vector.hpp>
#include <psi/vm/allocators/crt.hpp>
#if PSI_VM_HAS_DLMALLOC
#include <psi/vm/allocators/dlmalloc.hpp>
#endif

#if PSI_VM_HAS_MIMALLOC
#include <psi/vm/allocators/mimalloc.hpp>
#include <psi/vm/allocators/mi_scoped_heap.hpp>
#include <psi/vm/allocators/mi_heap.hpp>
#endif

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <ranges>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// vector<heap_storage> -- heap-allocated vector via storage template
////////////////////////////////////////////////////////////////////////////////

TEST( vector_storage, heap_storage_basic )
{
    using storage = heap_storage<int>;
    vector<storage> vec;
    EXPECT_TRUE( vec.empty() );
    EXPECT_EQ( vec.size(), 0u );

    vec.push_back( 42 );
    vec.push_back( 13 );
    vec.push_back( 7 );
    EXPECT_EQ( vec.size(), 3u );
    EXPECT_EQ( vec[ 0 ], 42 );
    EXPECT_EQ( vec[ 1 ], 13 );
    EXPECT_EQ( vec[ 2 ], 7 );
}

TEST( vector_storage, heap_storage_grow )
{
    using storage = heap_storage<std::uint32_t>;
    vector<storage> vec;

    for ( std::uint32_t i{ 0 }; i < 10000; ++i )
        vec.push_back( i );

    EXPECT_EQ( vec.size(), 10000u );
    for ( std::uint32_t i{ 0 }; i < 10000; ++i )
        EXPECT_EQ( vec[ i ], i );
}

TEST( vector_storage, heap_storage_append_range )
{
    using storage = heap_storage<double>;
    vector<storage> vec;

    vec.append_range({ 1.0, 2.0, 3.0, 4.0, 5.0 });
    EXPECT_EQ( vec.size(), 5u );
    EXPECT_EQ( vec.front(), 1.0 );
    EXPECT_EQ( vec.back(), 5.0 );
}

TEST( vector_storage, heap_storage_erase )
{
    using storage = heap_storage<int>;
    vector<storage> vec;

    vec.append_range({ 1, 2, 3, 4, 5 });
    vec.erase( vec.begin() + 2 ); // erase 3
    EXPECT_EQ( vec.size(), 4u );
    EXPECT_EQ( vec[ 0 ], 1 );
    EXPECT_EQ( vec[ 1 ], 2 );
    EXPECT_EQ( vec[ 2 ], 4 );
    EXPECT_EQ( vec[ 3 ], 5 );
}


////////////////////////////////////////////////////////////////////////////////
// vector<fixed_storage> -- fixed-capacity vector via storage template
////////////////////////////////////////////////////////////////////////////////

TEST( vector_storage, fixed_storage_basic )
{
    using storage = fixed_storage<int, 16>;
    vector<storage> vec;
    EXPECT_TRUE( vec.empty() );
    EXPECT_EQ( vec.capacity(), 16u );

    vec.push_back( 1 );
    vec.push_back( 2 );
    vec.push_back( 3 );
    EXPECT_EQ( vec.size(), 3u );
    EXPECT_EQ( vec[ 0 ], 1 );
    EXPECT_EQ( vec[ 2 ], 3 );
}

TEST( vector_storage, fixed_storage_fill )
{
    using storage = fixed_storage<int, 64>;
    vector<storage> vec;

    for ( int i{ 0 }; i < 64; ++i )
        vec.push_back( i );

    EXPECT_EQ( vec.size(), 64u );
    for ( int i{ 0 }; i < 64; ++i )
        EXPECT_EQ( vec[ i ], i );
}


////////////////////////////////////////////////////////////////////////////////
// Verify tr_vector alias: tr_vector<T> is now vector<heap_storage<T>>
////////////////////////////////////////////////////////////////////////////////

TEST( vector_storage, tr_vector_is_heap_storage_alias )
{
    // tr_vector<T> = vector<heap_storage<T>> (same type after aliasing)
    static_assert( std::is_same_v<tr_vector<int>, vector<heap_storage<int>>> );

    tr_vector<int> vec;
    for ( int i{ 0 }; i < 100; ++i )
        vec.push_back( i );

    EXPECT_EQ( vec.size(), 100u );
    EXPECT_EQ( vec[ 0 ], 0 );
    EXPECT_EQ( vec[ 99 ], 99 );

    // Copy and move semantics
    auto copy{ vec };
    EXPECT_TRUE( std::ranges::equal( vec, copy ) );

    auto moved{ std::move( copy ) };
    EXPECT_TRUE( copy.empty() );
    EXPECT_TRUE( std::ranges::equal( vec, moved ) );
}

TEST( vector_storage, fc_vector_is_fixed_storage_alias )
{
    // fc_vector<T, N> = vector<fixed_storage<T, N>> (same type after aliasing)
    static_assert( std::is_same_v<fc_vector<int, 32>, vector<fixed_storage<int, 32>>> );

    fc_vector<int, 32> vec;
    for ( int i{ 0 }; i < 32; ++i )
        vec.push_back( i );

    EXPECT_EQ( vec.size(), 32u );
    EXPECT_EQ( vec[ 0 ], 0 );
    EXPECT_EQ( vec[ 31 ], 31 );

    // Move semantics
    auto moved{ std::move( vec ) };
    EXPECT_TRUE( vec.empty() );
    EXPECT_EQ( moved.size(), 32u );
    EXPECT_TRUE( std::ranges::equal( moved, std::views::iota( 0, 32 ) ) );
}


////////////////////////////////////////////////////////////////////////////////
// dlmalloc allocator (via Boost.Container's compiled alloc_lib)
// Only available when the host project compiles alloc_lib.c and defines
// PSI_VM_HAS_DLMALLOC=1. In the standalone psi.vm build the compiled
// allocator library is not available.
////////////////////////////////////////////////////////////////////////////////

#if PSI_VM_HAS_DLMALLOC

TEST( vector_storage, dlmalloc_allocator_basic )
{
    using storage = heap_storage<int, std::size_t, dlmalloc_allocator<int>>;
    vector<storage> vec;

    vec.push_back( 1 );
    vec.push_back( 2 );
    vec.push_back( 3 );
    EXPECT_EQ( vec.size(), 3u );
    EXPECT_EQ( vec[ 0 ], 1 );
    EXPECT_EQ( vec[ 2 ], 3 );
}

TEST( vector_storage, dlmalloc_allocator_grow )
{
    using storage = heap_storage<std::uint32_t, std::size_t, dlmalloc_allocator<std::uint32_t>>;
    vector<storage> vec;

    for ( std::uint32_t i{ 0 }; i < 5000; ++i )
        vec.push_back( i );

    EXPECT_EQ( vec.size(), 5000u );
    for ( std::uint32_t i{ 0 }; i < 5000; ++i )
        EXPECT_EQ( vec[ i ], i );
}

TEST( vector_storage, dlmalloc_allocator_try_expand )
{
    // dlmalloc supports try_expand (boost_cont_grow)
    static_assert( has_try_expand<dlmalloc_allocator<int>> );

    using storage = heap_storage<int, std::size_t, dlmalloc_allocator<int>>;
    vector<storage> vec;

    // Fill and then grow -- should exercise try_expand path
    for ( int i{ 0 }; i < 100; ++i )
        vec.push_back( i );
    for ( int i{ 100 }; i < 1000; ++i )
        vec.push_back( i );

    EXPECT_EQ( vec.size(), 1000u );
    EXPECT_EQ( vec[ 0 ], 0 );
    EXPECT_EQ( vec[ 999 ], 999 );
}

#endif // PSI_VM_HAS_DLMALLOC


////////////////////////////////////////////////////////////////////////////////
// Incomplete type support: vector<heap_storage<T>> with forward-declared T
////////////////////////////////////////////////////////////////////////////////

struct incomplete_type; // forward declaration only -- never completed in this TU

// Verify that the type aliases and storage class can be instantiated with
// an incomplete type. The key: sizeof(T) is only needed in method bodies
// (deferred instantiation), not in the class definition.
static_assert( sizeof( heap_storage<incomplete_type> ) > 0 );
static_assert( sizeof( vector<heap_storage<incomplete_type>> ) > 0 );

// tr_vector<T> = vector<heap_storage<T>> so it should also work
static_assert( sizeof( tr_vector<incomplete_type> ) > 0 );


////////////////////////////////////////////////////////////////////////////////
// Storage non-copyability: storages manage raw memory, not elements
////////////////////////////////////////////////////////////////////////////////

static_assert( !std::is_copy_constructible_v<heap_storage<int>> );
static_assert( !std::is_copy_assignable_v   <heap_storage<int>> );
static_assert(  std::is_move_constructible_v<heap_storage<int>> );
static_assert(  std::is_move_assignable_v   <heap_storage<int>> );

static_assert( !std::is_copy_constructible_v<fixed_storage<int, 16>> );
static_assert( !std::is_copy_assignable_v   <fixed_storage<int, 16>> );
static_assert(  std::is_move_constructible_v<fixed_storage<int, 16>> );
static_assert(  std::is_move_assignable_v   <fixed_storage<int, 16>> );

// But vector<Storage> IS copyable (copies elements, not raw memory)
static_assert(  std::is_copy_constructible_v<vector<heap_storage<int>>> );
static_assert(  std::is_copy_constructible_v<tr_vector<int>> );
static_assert(  std::is_copy_constructible_v<fc_vector<int, 16>> );


////////////////////////////////////////////////////////////////////////////////
// Allocator traits verification
////////////////////////////////////////////////////////////////////////////////

// CRT allocator: no try_expand support on any platform
static_assert( !crt_allocator<int>::try_expand_supports_null );

#if PSI_VM_HAS_DLMALLOC
static_assert( has_try_expand<dlmalloc_allocator<int>> );
static_assert( !dlmalloc_allocator<int>::try_expand_supports_null );
static_assert(  dlmalloc_allocator<int>::guaranteed_in_place_shrink );
static_assert(  has_try_shrink_in_place<dlmalloc_allocator<int>> );
#endif

#if PSI_VM_HAS_MIMALLOC
static_assert( has_try_expand<mimalloc_allocator<int>> );
static_assert( !mimalloc_allocator<int>::try_expand_supports_null );
static_assert( !mimalloc_allocator<int>::guaranteed_in_place_shrink );
#endif


////////////////////////////////////////////////////////////////////////////////
// mimalloc allocator (when PSI_VM_HAS_MIMALLOC is defined)
////////////////////////////////////////////////////////////////////////////////

#if PSI_VM_HAS_MIMALLOC

TEST( vector_storage, mimalloc_allocator_basic )
{
    using storage = heap_storage<int, std::size_t, mimalloc_allocator<int>>;
    vector<storage> vec;

    vec.push_back( 10 );
    vec.push_back( 20 );
    vec.push_back( 30 );
    EXPECT_EQ( vec.size(), 3u );
    EXPECT_EQ( vec[ 0 ], 10 );
    EXPECT_EQ( vec[ 2 ], 30 );
}

TEST( vector_storage, mimalloc_allocator_grow )
{
    using storage = heap_storage<std::uint32_t, std::size_t, mimalloc_allocator<std::uint32_t>>;
    vector<storage> vec;

    for ( std::uint32_t i{ 0 }; i < 10000; ++i )
        vec.push_back( i );

    EXPECT_EQ( vec.size(), 10000u );
    for ( std::uint32_t i{ 0 }; i < 10000; ++i )
        EXPECT_EQ( vec[ i ], i );
}

TEST( vector_storage, mimalloc_allocator_try_expand )
{
    // mimalloc supports try_expand (mi_expand)
    static_assert( has_try_expand<mimalloc_allocator<int>> );

    using storage = heap_storage<int, std::size_t, mimalloc_allocator<int>>;
    vector<storage> vec;

    for ( int i{ 0 }; i < 100; ++i )
        vec.push_back( i );
    for ( int i{ 100 }; i < 1000; ++i )
        vec.push_back( i );

    EXPECT_EQ( vec.size(), 1000u );
    EXPECT_EQ( vec[ 0 ], 0 );
    EXPECT_EQ( vec[ 999 ], 999 );
}

TEST( vector_storage, mimalloc_vs_crt_equivalence )
{
    vector<heap_storage<int, std::size_t, crt_allocator<int>>> crt_vec;
    vector<heap_storage<int, std::size_t, mimalloc_allocator<int>>>    mi_vec;

    for ( int i{ 0 }; i < 500; ++i )
    {
        crt_vec.push_back( i );
        mi_vec.push_back( i );
    }

    EXPECT_EQ( crt_vec.size(), mi_vec.size() );
    EXPECT_TRUE( std::ranges::equal( crt_vec, mi_vec ) );
}

#endif // PSI_VM_HAS_MIMALLOC


////////////////////////////////////////////////////////////////////////////////
// mi_scoped_heap_allocator -- pool/zone allocator using mimalloc per-heap API
////////////////////////////////////////////////////////////////////////////////

#if PSI_VM_HAS_MIMALLOC

TEST( vector_storage, scoped_heap_basic )
{
    using alloc   = mi_scoped_heap_allocator<int>;
    using storage = heap_storage<int, std::size_t, alloc>;

    mi_heap_scope scope;
    vector<storage> vec;

    vec.push_back( 1 );
    vec.push_back( 2 );
    vec.push_back( 3 );
    EXPECT_EQ( vec.size(), 3u );
    EXPECT_EQ( vec[ 0 ], 1 );
    EXPECT_EQ( vec[ 1 ], 2 );
    EXPECT_EQ( vec[ 2 ], 3 );
    // scope dtor: mi_heap_destroy frees everything at once
}

TEST( vector_storage, scoped_heap_grow )
{
    using alloc   = mi_scoped_heap_allocator<std::uint32_t>;
    using storage = heap_storage<std::uint32_t, std::size_t, alloc>;

    mi_heap_scope scope;
    vector<storage> vec;

    for ( std::uint32_t i{ 0 }; i < 10000; ++i )
        vec.push_back( i );

    EXPECT_EQ( vec.size(), 10000u );
    for ( std::uint32_t i{ 0 }; i < 10000; ++i )
        EXPECT_EQ( vec[ i ], i );
    // scope dtor frees all
}

TEST( vector_storage, scoped_heap_no_scope_fallback )
{
    // When no mi_heap_scope is active, allocations go through default mimalloc
    using alloc   = mi_scoped_heap_allocator<int>;
    using storage = heap_storage<int, std::size_t, alloc>;

    vector<storage> vec;
    vec.push_back( 42 );
    vec.push_back( 7 );
    EXPECT_EQ( vec.size(), 2u );
    EXPECT_EQ( vec[ 0 ], 42 );
    EXPECT_EQ( vec[ 1 ], 7 );
    // normal deallocation in destructor via mi_free
}

TEST( vector_storage, scoped_heap_release )
{
    // scope.release() transfers allocations to default heap -- data survives
    using alloc   = mi_scoped_heap_allocator<int>;
    using storage = heap_storage<int, std::size_t, alloc>;

    vector<storage> vec;
    {
        mi_heap_scope scope;
        vec.push_back( 10 );
        vec.push_back( 20 );
        vec.push_back( 30 );
        scope.release(); // transfer to default heap
    } // scope dtor is a no-op (heap_ == nullptr)

    // Data survives the scope
    EXPECT_EQ( vec.size(), 3u );
    EXPECT_EQ( vec[ 0 ], 10 );
    EXPECT_EQ( vec[ 1 ], 20 );
    EXPECT_EQ( vec[ 2 ], 30 );
    // normal deallocation via mi_free
}

TEST( vector_storage, scoped_heap_multiple_vectors )
{
    using alloc   = mi_scoped_heap_allocator<int>;
    using storage = heap_storage<int, std::size_t, alloc>;

    mi_heap_scope scope;

    vector<storage> v1, v2, v3;
    for ( int i{ 0 }; i < 1000; ++i )
    {
        v1.push_back( i );
        v2.push_back( i * 2 );
        v3.push_back( i * 3 );
    }

    EXPECT_EQ( v1.size(), 1000u );
    EXPECT_EQ( v2.size(), 1000u );
    EXPECT_EQ( v3.size(), 1000u );
    EXPECT_EQ( v1[ 999 ], 999 );
    EXPECT_EQ( v2[ 999 ], 1998 );
    EXPECT_EQ( v3[ 999 ], 2997 );
    // scope dtor: one mi_heap_destroy frees all 3 vectors' allocations at once
}

TEST( vector_storage, scoped_heap_try_expand )
{
    // mi_scoped_heap_allocator supports try_expand (mi_expand)
    static_assert( has_try_expand<mi_scoped_heap_allocator<int>> );

    using alloc   = mi_scoped_heap_allocator<int>;
    using storage = heap_storage<int, std::size_t, alloc>;

    mi_heap_scope scope;
    vector<storage> vec;

    for ( int i{ 0 }; i < 100; ++i )
        vec.push_back( i );
    for ( int i{ 100 }; i < 1000; ++i )
        vec.push_back( i );

    EXPECT_EQ( vec.size(), 1000u );
    EXPECT_EQ( vec[ 0 ], 0 );
    EXPECT_EQ( vec[ 999 ], 999 );
}

// Allocator traits for scoped heap allocator
static_assert( has_try_expand<mi_scoped_heap_allocator<int>> );
static_assert( !mi_scoped_heap_allocator<int>::try_expand_supports_null );
static_assert( !mi_scoped_heap_allocator<int>::guaranteed_in_place_shrink );


////////////////////////////////////////////////////////////////////////////////
// mi_heap_allocator -- stateful allocator carrying mi_heap_t* instance state
////////////////////////////////////////////////////////////////////////////////

// Verify that the stateful allocator is non-empty (carries a pointer)
static_assert( sizeof( mi_heap_allocator<int> ) == sizeof( void * ) );

// Verify that heap_storage with a stateful allocator is larger than with a
// stateless one (EBO only collapses empty bases).
static_assert( sizeof( heap_storage<int, std::size_t, mi_heap_allocator<int>> )
             > sizeof( heap_storage<int, std::size_t, mimalloc_allocator<int>> ) );

// Allocator traits
static_assert( has_try_expand<mi_heap_allocator<int>> );
static_assert( !mi_heap_allocator<int>::try_expand_supports_null );
static_assert( !mi_heap_allocator<int>::guaranteed_in_place_shrink );

// Trivially copyable (non-owning pointer) -- heap_storage can memcpy-move it
static_assert( std::is_trivially_copyable_v<mi_heap_allocator<int>> );

TEST( vector_storage, mi_heap_allocator_basic )
{
    using alloc   = mi_heap_allocator<int>;
    using storage = heap_storage<int, std::size_t, alloc>;

    mi_heap_scope scope;
    vector<storage> vec{ storage{ alloc{ scope.heap() } } };

    vec.push_back( 1 );
    vec.push_back( 2 );
    vec.push_back( 3 );
    EXPECT_EQ( vec.size(), 3u );
    EXPECT_EQ( vec[ 0 ], 1 );
    EXPECT_EQ( vec[ 1 ], 2 );
    EXPECT_EQ( vec[ 2 ], 3 );
}

TEST( vector_storage, mi_heap_allocator_grow )
{
    using alloc   = mi_heap_allocator<std::uint32_t>;
    using storage = heap_storage<std::uint32_t, std::size_t, alloc>;

    mi_heap_scope scope;
    vector<storage> vec{ storage{ alloc{ scope.heap() } } };

    for ( std::uint32_t i{ 0 }; i < 10000; ++i )
        vec.push_back( i );

    EXPECT_EQ( vec.size(), 10000u );
    for ( std::uint32_t i{ 0 }; i < 10000; ++i )
        EXPECT_EQ( vec[ i ], i );
}

TEST( vector_storage, mi_heap_allocator_try_expand )
{
    using alloc   = mi_heap_allocator<int>;
    using storage = heap_storage<int, std::size_t, alloc>;

    mi_heap_scope scope;
    vector<storage> vec{ storage{ alloc{ scope.heap() } } };

    for ( int i{ 0 }; i < 100; ++i )
        vec.push_back( i );
    for ( int i{ 100 }; i < 1000; ++i )
        vec.push_back( i );

    EXPECT_EQ( vec.size(), 1000u );
    EXPECT_EQ( vec[ 0 ], 0 );
    EXPECT_EQ( vec[ 999 ], 999 );
}

TEST( vector_storage, mi_heap_allocator_move_preserves_heap )
{
    // When moving a vector with a stateful allocator, the heap pointer
    // should transfer correctly.
    using alloc   = mi_heap_allocator<int>;
    using storage = heap_storage<int, std::size_t, alloc>;

    mi_heap_scope scope;
    vector<storage> v1{ storage{ alloc{ scope.heap() } } };
    v1.push_back( 10 );
    v1.push_back( 20 );

    auto v2{ std::move( v1 ) };
    EXPECT_TRUE( v1.empty() );
    EXPECT_EQ( v2.size(), 2u );
    EXPECT_EQ( v2[ 0 ], 10 );
    EXPECT_EQ( v2[ 1 ], 20 );

    // Continue using moved-to vector (allocations go through the same heap)
    v2.push_back( 30 );
    EXPECT_EQ( v2.size(), 3u );
    EXPECT_EQ( v2[ 2 ], 30 );
}

TEST( vector_storage, mi_heap_allocator_multiple_vectors_same_heap )
{
    // Multiple vectors sharing the same heap -- all freed at once by scope dtor
    using alloc   = mi_heap_allocator<int>;
    using storage = heap_storage<int, std::size_t, alloc>;

    mi_heap_scope scope;
    alloc a{ scope.heap() };

    vector<storage> v1{ storage{ a } };
    vector<storage> v2{ storage{ a } };
    vector<storage> v3{ storage{ a } };

    for ( int i{ 0 }; i < 1000; ++i )
    {
        v1.push_back( i );
        v2.push_back( i * 2 );
        v3.push_back( i * 3 );
    }

    EXPECT_EQ( v1.size(), 1000u );
    EXPECT_EQ( v2.size(), 1000u );
    EXPECT_EQ( v3.size(), 1000u );
    EXPECT_EQ( v1[ 999 ], 999 );
    EXPECT_EQ( v2[ 999 ], 1998 );
    EXPECT_EQ( v3[ 999 ], 2997 );
}

TEST( vector_storage, mi_heap_allocator_release_survives_scope )
{
    // scope.release() transfers allocations to default heap.
    // The stateful allocator's heap pointer becomes stale, but existing
    // allocations (now owned by default heap) remain valid and can be
    // freed via mi_free.
    using alloc   = mi_heap_allocator<int>;
    using storage = heap_storage<int, std::size_t, alloc>;

    vector<storage> vec;
    {
        mi_heap_scope scope;
        vec = vector<storage>{ storage{ alloc{ scope.heap() } } };
        vec.push_back( 10 );
        vec.push_back( 20 );
        vec.push_back( 30 );
        scope.release(); // transfer allocations to default heap
    }

    // Data survives -- existing allocations are fine
    EXPECT_EQ( vec.size(), 3u );
    EXPECT_EQ( vec[ 0 ], 10 );
    EXPECT_EQ( vec[ 1 ], 20 );
    EXPECT_EQ( vec[ 2 ], 30 );
    // vec dtor: mi_free works for allocations now on default heap
}

#endif // PSI_VM_HAS_MIMALLOC


////////////////////////////////////////////////////////////////////////////////
// Growth policy: verify geometric growth at vector<> level
////////////////////////////////////////////////////////////////////////////////

TEST( vector_storage, growth_policy_geometric )
{
    // Default tr_vector has 3/2 (1.5x) geometric growth
    tr_vector<int> vec;
    ASSERT_TRUE( vec.empty() );

    // Push elements and verify capacity grows geometrically (> linearly)
    vec.push_back( 0 );
    auto cap_after_1{ vec.capacity() };
    EXPECT_GE( cap_after_1, 1u );

    // Fill to capacity, then push one more to trigger growth
    while ( vec.size() < cap_after_1 )
        vec.push_back( static_cast<int>( vec.size() ) );
    vec.push_back( static_cast<int>( vec.size() ) );

    auto cap_after_grow{ vec.capacity() };
    // Capacity should have grown by at least 1.5x (the allocator may round up)
    EXPECT_GE( cap_after_grow, cap_after_1 * 3 / 2 );

    // Verify all elements are correct
    for ( std::size_t i{ 0 }; i < vec.size(); ++i )
        EXPECT_EQ( vec[ i ], static_cast<int>( i ) );
}

TEST( vector_storage, growth_policy_disabled )
{
    // Growth with num==den (1/1) means disabled -- exact-size allocations
    using storage = heap_storage<int>;
    vector<storage, geometric_growth{ 1, 1 }> vec;

    vec.push_back( 1 );
    vec.push_back( 2 );
    vec.push_back( 3 );
    EXPECT_EQ( vec.size(), 3u );
    // With growth disabled, capacity == size (exact-size allocations,
    // though allocator may over-allocate slightly)
    EXPECT_LE( vec.capacity(), vec.size() + 1u );
    EXPECT_EQ( vec[ 0 ], 1 );
    EXPECT_EQ( vec[ 2 ], 3 );
}


//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
