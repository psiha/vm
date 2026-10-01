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
#include <psi/vm/containers/heap_vector.hpp>
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

#include <limits>
#include <stdexcept>

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
// Verify heap_vector alias: heap_vector<T> is now vector<heap_storage<T>>
////////////////////////////////////////////////////////////////////////////////

TEST( vector_storage, heap_vector_is_heap_storage_alias )
{
    // heap_vector<T> = vector<heap_storage<T>> (same type after aliasing)
    static_assert( std::is_same_v<heap_vector<int>, vector<heap_storage<int>>> );

    heap_vector<int> vec;
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
static_assert( sizeof( vector<heap_storage<incomplete_type>, geometric_growth{}, true> ) > 0 );

// heap_vector<T, ..., support_incomplete_types=true> for recursive / incomplete T
static_assert( sizeof( heap_vector<incomplete_type, std::size_t, {}, {}, true> ) > 0 );

// Recursive strong-typedef: struct Body : vector<Rec> while Rec is still incomplete.
// std::vector<Rec> tolerates this; heap_vector<Rec, ..., true> must too.
struct recursive_record;
using recursive_vec = heap_vector<recursive_record, std::size_t, {}, {}, true>;
struct recursive_body : recursive_vec {
    using recursive_vec::recursive_vec;
};
static_assert( sizeof( recursive_body ) > 0 );

// Recursive variant + vector body.
struct variant_record;
using variant_body_vec = heap_vector<variant_record, std::size_t, {}, {}, true>;
struct variant_body : variant_body_vec {
    using variant_body_vec::variant_body_vec;
};
using variant_record_variant = std::variant<int, variant_body>;
struct variant_record : variant_record_variant {
    using variant_record_variant::variant_record_variant;
};
static_assert( sizeof( variant_record ) > 0 );

// Nested template + external member holding a conjoined vector body (filter_ast-style).
// Node stays forward-declared only (heap_vector element type); holder proves the
// external-member pattern compiles. variant_record above covers variant + body.
namespace recursive_typedef_compile_tests {

template <typename... AltTypes> struct Node;

template <typename... AltTypes>
struct node_expr {
    using node = Node<AltTypes...>;
    using nodes = heap_vector<node, std::uint32_t, {}, {}, true>;
    struct [[ clang::trivial_abi ]] conjoined_nodes : nodes {
        static constexpr bool is_trivially_moveable{ false };
        using nodes::nodes;
    };
};

struct leaf { int x; };

struct holder {
    typename node_expr<leaf>::conjoined_nodes body;
};

static_assert( sizeof( holder ) > 0 );

} // namespace recursive_typedef_compile_tests

// Forward-declared element members inside a nested payload hierarchy.
// This mirrors std::vector-tolerated incomplete-type composition.
namespace incomplete_nested_member_vector_tests {

struct PayloadValueA;
struct PayloadValueB;

using ValueVectorA = heap_vector<PayloadValueA, std::uint32_t, {}, {}, true>;
using ValueVectorB = heap_vector<PayloadValueB, std::uint32_t, {}, {}, true>;

struct NestedRow {
    ValueVectorA valuesA;
    ValueVectorB valuesB;
};

struct NestedPayload {
    heap_vector<NestedRow, std::uint32_t, {}, {}, true> rows;
};

static_assert( sizeof( NestedRow ) > 0 );
static_assert( sizeof( NestedPayload ) > 0 );

struct PayloadValueA {
    double value{};
};

struct PayloadValueB {
    std::uint32_t value{};
};

TEST( vector_storage, incomplete_nested_member_vectors )
{
    heap_vector<NestedPayload, std::uint32_t, {}, {}, true> payloads;
    payloads.emplace_back();
    payloads.back().rows.emplace_back();
    payloads.back().rows.back().valuesA.emplace_back( PayloadValueA{ 1.0 } );
    payloads.back().rows.back().valuesB.emplace_back( PayloadValueB{ 7 } );
    EXPECT_EQ( payloads.size(), 1u );
    EXPECT_EQ( payloads.back().rows.size(), 1u );
    EXPECT_EQ( payloads.back().rows.back().valuesA.size(), 1u );
    EXPECT_EQ( payloads.back().rows.back().valuesB.size(), 1u );
}

} // namespace incomplete_nested_member_vector_tests


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
static_assert(  std::is_copy_constructible_v<heap_vector<int>> );
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
    // Default heap_vector has 3/2 (1.5x) geometric growth
    heap_vector<int> vec;
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


// A length the storage's byte counter cannot express is REPORTED, not
// truncated, and it is reported whatever the overcommit policy: the request is
// a precondition violation, while the policy governs whether allocation can
// fail, which is a different question about a different step.
TEST( vector_storage, length_past_the_byte_counters_range_is_reported )
{
    // A narrow byte counter under a wider element: the ceiling is the
    // counter's byte range divided by the element size, so it sits well below
    // what the element counter itself holds, and a length between the two is
    // expressible but unservable.
    using storage = heap_storage<std::uint32_t, std::uint32_t>;
    using vec_t   = vector<storage>;

    constexpr auto ceiling{ storage::max_size() };
    static_assert( ceiling < std::numeric_limits<std::uint32_t>::max() );

    static_assert( storage::length_error_is_reportable );

    vec_t vec;
    vec.resize( 4 ); // the ordinary case is unaffected
    EXPECT_EQ( vec.size(), 4u );

    // Refused before anything is allocated, so driving it costs nothing.
    EXPECT_THROW( vec.resize( ceiling + 1 ), std::length_error );
    EXPECT_EQ( vec.size(), 4u );
}

// A request of exactly max_size() is servable, and the capacity it leaves
// behind covers it. At the ceiling the allocator's usable size (request plus
// slack) can exceed what the narrow counter expresses, so it reads back
// wrapped; the storage must not cache that as its capacity. (Only a shell that
// reports usable slack is read back at all - the MSVC CRT one is not, and there
// this holds trivially.)
TEST( vector_storage, a_request_at_the_byte_counters_ceiling_keeps_its_capacity )
{
    // A 16-bit counter puts the ceiling within cheap reach (64 KiB of
    // elements) while keeping it a reportable one.
    using storage = heap_storage<std::uint32_t, std::uint16_t>;
    constexpr auto ceiling{ storage::max_size() };
    static_assert( ceiling < std::numeric_limits<std::uint16_t>::max() );

    vector<storage> vec;
    vec.resize( ceiling );
    EXPECT_EQ( vec.size(), ceiling );

    // Read through a volatile: capacity() itself assumes that it covers
    // size(), so an optimiser is otherwise free to fold a comparison of the two
    // away, and pass a capacity that does not cover it.
    std::uint16_t volatile const capacity{ vec.capacity() };
    EXPECT_EQ( static_cast<std::uint16_t>( capacity ), ceiling );
}

// The ceiling is itself a reachable size: geometric growth that would overshoot
// it is clamped to it rather than refused, so appending one element at a time
// fills the storage all the way up. Refusing the overshoot instead would reject
// every size from roughly ceiling / growth-factor upwards - sizes the storage
// can hold - as soon as the next growth step happened to cross the ceiling.
TEST( vector_storage, appending_reaches_the_byte_counters_ceiling )
{
    // A 16-bit counter puts the ceiling within cheap reach (64 KiB of
    // elements) while keeping it a reportable one.
    using storage = heap_storage<std::uint32_t, std::uint16_t>;
    using vec_t   = vector<storage>;

    constexpr auto ceiling{ storage::max_size() };
    static_assert( ceiling < std::numeric_limits<std::uint16_t>::max() );
    static_assert( storage::length_error_is_reportable );

    {
        vec_t vec;
        for ( std::uint32_t i{ 0 }; i < ceiling; ++i )
            vec.push_back( i );
        EXPECT_EQ( vec.size(), ceiling );
        EXPECT_EQ( vec.front(), 0u );
        EXPECT_EQ( vec.back (), ceiling - 1u );

        // Past the ceiling the request itself is unservable, and still refused.
        EXPECT_THROW( vec.push_back( 0 ), std::length_error );
        EXPECT_EQ( vec.size(), ceiling );
    }
    {
        // The same rule for bulk appends, which size their headroom with the
        // container's growth policy rather than the storage's.
        vec_t vec;
        for ( std::uint32_t i{ 0 }; i < ceiling; ++i )
            vec.grow_by_amortized( 1, value_init );
        EXPECT_EQ( vec.size(), ceiling );
        EXPECT_THROW( vec.grow_by_amortized( 1, value_init ), std::length_error );
        EXPECT_EQ( vec.size(), ceiling );
    }
}

// The width axis: a 64-bit byte counter cannot be reached by any plausible
// size computation, so crossing it stays an assertion and the storage does not
// acquire a throwing resize for nothing.
TEST( vector_storage, a_64_bit_byte_counter_keeps_a_noexcept_resize )
{
    using wide_storage = heap_storage<std::uint32_t, std::size_t>;
    static_assert( sizeof( std::size_t ) == sizeof( std::uint64_t ) );

    static_assert( !wide_storage::length_error_is_reportable );

    vector<wide_storage> vec;
    vec.resize( 4 );
    EXPECT_EQ( vec.size(), 4u );
}

// An element that must be moved one by one grows through the allocator's
// in-place expansion first, and that call cannot throw. A length past the
// ceiling must therefore be refused before it is attempted - reported where the
// caller can catch it, rather than escaping a non-throwing call and terminating.
struct moved_one_by_one
{
    static constexpr bool is_trivially_moveable{ false }; // forbids realloc
    std::uint32_t value;
};

TEST( vector_storage, length_past_the_ceiling_is_reported_on_the_in_place_expansion_path )
{
    using element = moved_one_by_one;
    using storage = heap_storage<element, std::uint16_t>;
    using vec_t   = vector<storage>;

    static_assert( !is_trivially_moveable<element> );
    static_assert( storage::length_error_is_reportable );

    if constexpr ( !has_try_expand<storage::allocator_type> )
    {
        GTEST_SKIP() << "the allocator offers no in-place expansion: growth relocates, and the refusal propagates from there";
    }
    else
    {
        constexpr auto ceiling{ storage::max_size() };

        vec_t vec;
        vec.resize( 4 ); // something to expand in place
        auto constexpr past_it{ static_cast<std::uint16_t>( ceiling + 1 ) };
        EXPECT_THROW( vec.reserve( past_it ), std::length_error );
        EXPECT_EQ( vec.size(), 4u );

        // The non-relocating variant only ASKS for an in-place expansion, and a
        // length past the ceiling is one it cannot get: a plain `false`. (It
        // asks the storage only where the storage offers the query - MSVC-ABI
        // builds; elsewhere it answers `false` without asking.)
        EXPECT_FALSE( vec.stable_reserve( past_it ) );
        EXPECT_EQ( vec.size(), 4u );
    }
}

// Lengths the container forms itself - `size() + delta` sums, a range's
// std::size_t extent - are formed wider than a narrow size_type, so one past
// what the counter can express is refused, where forming it in the counter
// would wrap it into a small length the storage happily serves. With a 1-byte
// element the storage's ceiling IS the counter's maximum, so the storage's own
// ceiling check cannot see such a length at all.
namespace
{
    using narrow_bytes     = heap_storage<std::uint8_t, std::uint32_t>;
    using narrow_bytes_vec = vector<narrow_bytes>;
    static_assert( narrow_bytes::max_size() == std::numeric_limits<std::uint32_t>::max() );

    // A sized range that only reports its extent: refused before it is read.
    struct extent_only
    {
        std::size_t extent;
        std::uint8_t const * begin() const noexcept { return nullptr; }
        std::uint8_t const * end  () const noexcept { return nullptr; }
        std::size_t          size () const noexcept { return extent; }
    };
    // Four past the counter: converted to it, this is the length 4.
    constexpr std::size_t past_the_counter{ std::size_t{ std::numeric_limits<std::uint32_t>::max() } + 5 };
} // anonymous namespace

TEST( vector_storage, a_size_plus_delta_sum_past_a_narrow_counter_is_reported )
{
    static_assert( sizeof( std::size_t ) > sizeof( std::uint32_t ) );
    narrow_bytes_vec vec;
    vec.resize( 4, value_init );

    // size() + delta is one past the counter's maximum: summed in the counter
    // it wraps to 3, a length the container already exceeds. Each is refused
    // before anything is allocated, so driving it costs nothing.
    constexpr auto delta{ std::numeric_limits<std::uint32_t>::max() };
    EXPECT_THROW( vec.grow_by          ( delta, value_init ), std::length_error );
    EXPECT_THROW( vec.grow_by_amortized( delta, value_init ), std::length_error );
    EXPECT_EQ( vec.size(), 4u );
}

TEST( vector_storage, an_insert_whose_sum_is_past_a_narrow_counter_is_reported )
{
    narrow_bytes_vec vec;
    vec.resize( 4, value_init );

    constexpr auto count{ std::numeric_limits<std::uint32_t>::max() };
    EXPECT_THROW( vec.insert( vec.begin(), count, std::uint8_t{ 7 } ), std::length_error );
    EXPECT_EQ( vec.size(), 4u );
}

TEST( vector_storage, a_range_extent_past_a_narrow_counter_is_reported )
{
    static_assert( sizeof( std::size_t ) > sizeof( std::uint32_t ) );
    narrow_bytes_vec vec;
    vec.resize( 4, value_init );

    EXPECT_THROW( vec.append_range( extent_only{ past_the_counter } ), std::length_error );
    EXPECT_THROW( vec.insert_range( vec.begin(), extent_only{ past_the_counter } ), std::length_error );
    EXPECT_THROW( vec.assign      ( extent_only{ past_the_counter } ), std::length_error );
    EXPECT_EQ( vec.size(), 4u );
}

TEST( vector_storage, appending_past_a_narrow_counter_is_reported_for_a_1_byte_element )
{
    // A 16-bit counter puts its maximum within cheap reach (64 KiB).
    using storage = heap_storage<std::uint8_t, std::uint16_t>;
    static_assert( storage::max_size() == std::numeric_limits<std::uint16_t>::max() );

    vector<storage> vec;
    vec.resize( storage::max_size(), value_init );
    EXPECT_THROW( vec.push_back( 1 ), std::length_error );
    EXPECT_THROW( vec.grow_by( 1, value_init ), std::length_error );
    EXPECT_EQ( vec.size(), storage::max_size() );
    // A query does not throw: past the counter no capacity helps.
    EXPECT_FALSE( vec.stable_emplace_back( std::uint8_t{ 1 } ) );
    EXPECT_EQ( vec.size(), storage::max_size() );
}

// The storage-kind axis: a fixed-capacity container allocates nothing, so it
// has no byte ceiling to report and keeps the overflow contract its own policy
// argument selects - asserting by default, independently of any of the above.
TEST( vector_storage, a_fixed_capacity_container_keeps_its_own_overflow_policy )
{
    fc_vector<std::uint32_t, 4> vec;
    vec.resize( 4 );
    EXPECT_EQ( vec.size(),     4u );
    EXPECT_EQ( vec.capacity(), 4u );
    EXPECT_EQ( vec.max_size(), 4u );
}


////////////////////////////////////////////////////////////////////////////////
// Byte typed allocators take the byte shell path
////////////////////////////////////////////////////////////////////////////////

// The default storage stays trivially moveable (relocated by memcpy, passed
// in registers): the trait follows the shell allocator.
static_assert( is_trivially_moveable<heap_storage<int>> );
static_assert( is_trivially_moveable<heap_vector<int>> );
static_assert( is_trivially_moveable<heap_storage<int, std::uint32_t, crt_allocator<std::byte, std::uint32_t>>> );

namespace
{
    // A stateful byte allocator recording what heap_storage asks of it.
    struct byte_allocator_log
    {
        std::size_t allocated     {}; // bytes asked of the last allocate()
        std::size_t size_requested{}; // the hint size() last got
        std::size_t shrink_current{}; // the current size shrink_to() last got
        std::size_t deallocated   {}; // bytes deallocate() last got
        int         blocks        {}; // live blocks
    };

    struct logging_byte_allocator
    {
        using value_type    = std::byte;
        using pointer       = std::byte *;
        using const_pointer = std::byte const *;
        using size_type     = std::uint32_t;
        using base          = crt_allocator<std::byte, size_type>;

        static bool constexpr guaranteed_in_place_shrink            { false };
        static bool constexpr in_place_ops_require_default_alignment{ false };
        static bool constexpr size_reports_usable_capacity          { true  }; // (and reports exactly the request)

        byte_allocator_log * log;

        template <std::uint8_t alignment> pointer allocate( size_type const n ) { log->allocated = n; ++log->blocks; return base::allocate<alignment>( n ); }
        template <std::uint8_t alignment> void deallocate( pointer const p, size_type const n ) noexcept { log->deallocated = n; --log->blocks; base::deallocate<alignment>( p, n ); }
        template <std::uint8_t alignment> pointer grow_to  ( pointer const p, size_type const current, size_type const target )          { log->allocated = target; return base::grow_to  <alignment>( p, current, target ); }
        template <std::uint8_t alignment> pointer shrink_to( pointer const p, size_type const current, size_type const target ) noexcept { log->shrink_current = current; return base::shrink_to<alignment>( p, current, target ); }
        size_type size( const_pointer, size_type const requested ) const noexcept { log->size_requested = requested; return requested; }
    };

    template <typename Storage>
    concept has_external_allocation = requires( typename Storage::pointer p ) { Storage::allocate_external( 1 ); Storage::deallocate_external( p, 1 ); };
} // anonymous namespace

static_assert(  is_trivially_moveable<heap_storage<std::uint64_t, std::uint32_t, logging_byte_allocator>> ); // a pointer
static_assert(  has_external_allocation<heap_storage<int>> );
static_assert(  has_external_allocation<heap_storage<int, std::uint32_t, crt_allocator<std::byte, std::uint32_t>>> );
// Static, so they could only ever use the default state of a stateful allocator.
static_assert( !has_external_allocation<heap_storage<std::uint64_t, std::uint32_t, logging_byte_allocator>> );

// A byte typed allocator counts the block in bytes, gets the storage's own
// state (not a default constructed allocator), the cached capacity as the
// block's current size when it shrinks, and the requested capacity as a hint
// when it is asked for the block's size.
TEST( vector_storage, a_byte_typed_allocator_counts_bytes_and_keeps_its_state )
{
    using storage = heap_storage<std::uint64_t, std::uint32_t, logging_byte_allocator>;
    byte_allocator_log log;
    {
        storage initial{ logging_byte_allocator{ &log } };
        EXPECT_EQ( initial.get_allocator().log, &log );
        vector<storage> vec{ std::move( initial ) };

        vec.reserve( 100 );
        EXPECT_EQ( log.allocated     , 100 * sizeof( std::uint64_t ) );
        EXPECT_EQ( log.size_requested, 100 * sizeof( std::uint64_t ) );
        EXPECT_EQ( log.blocks, 1 );
        EXPECT_EQ( vec.capacity(), 100u );

        for ( std::uint64_t i{ 0 }; i < 10; ++i )
            vec.push_back( i );
        vec.shrink_to_fit();
        EXPECT_EQ( log.shrink_current, 100 * sizeof( std::uint64_t ) ) << "shrink got the live length, not the block's size";
        EXPECT_EQ( log.size_requested,  10 * sizeof( std::uint64_t ) );
        EXPECT_EQ( vec.capacity(), 10u );
        EXPECT_TRUE( std::ranges::equal( vec, std::views::iota( std::uint64_t{ 0 }, std::uint64_t{ 10 } ) ) );
    }
    EXPECT_EQ( log.deallocated, 10 * sizeof( std::uint64_t ) );
    EXPECT_EQ( log.blocks, 0 );
}

// A byte typed default allocator behaves like the default storage: the same
// path, the same block.
TEST( vector_storage, a_byte_typed_allocator_matches_the_default_storage )
{
    using byte_shell = heap_storage<std::uint32_t, std::uint32_t, crt_allocator<std::byte, std::uint32_t>>;
    static_assert( sizeof( byte_shell ) == sizeof( heap_storage<std::uint32_t, std::uint32_t> ) );
    vector<byte_shell> vec;
    for ( std::uint32_t i{ 0 }; i < 10000; ++i )
        vec.push_back( i );
    EXPECT_TRUE( std::ranges::equal( vec, std::views::iota( 0U, 10000U ) ) );
    vec.resize( 10 );
    vec.shrink_to_fit();
    EXPECT_TRUE( std::ranges::equal( vec, std::views::iota( 0U, 10U ) ) );
    auto * const p{ byte_shell::allocate_external( 7 ) };
    p[ 6 ] = 6;
    byte_shell::deallocate_external( p, 7 );
}


//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
