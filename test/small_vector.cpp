// Small-vector-specific tests: inline storage, transitions, type erasure, etc.
#include <psi/vm/containers/small_vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

inline constexpr small_vector_options msb_opts{ .layout = small_vector_layout::compact       };
inline constexpr small_vector_options pb_opts { .layout = small_vector_layout::pointer_based };
inline constexpr small_vector_options lsb_opts{ .layout = small_vector_layout::compact_lsb   };
inline constexpr small_vector_options emb_opts{ .layout = small_vector_layout::embedded      };
template <typename T, std::uint32_t N>
using msb_small_vector = small_vector<T, N, std::uint32_t, msb_opts>;
template <typename T, std::uint32_t N>
using pb_small_vector  = small_vector<T, N, std::size_t, pb_opts>;
template <typename T, std::uint32_t N>
using lsb_small_vector = small_vector<T, N, std::size_t, lsb_opts>;
template <typename T, std::uint32_t N>
using emb_small_vector  = small_vector<T, N, std::size_t, emb_opts>;

////////////////////////////////////////////////////////////////////////////////
// Compact layout tests (MSB flag — must use explicit compact opts, not default)
////////////////////////////////////////////////////////////////////////////////

TEST( small_vector_compact, inline_storage )
{
    msb_small_vector<int, 4> v;
    v.push_back( 1 );
    v.push_back( 2 );
    v.push_back( 3 );

    // Data should be within the object (inline buffer)
    auto const obj_begin{ reinterpret_cast<std::uintptr_t>( &v ) };
    auto const obj_end  { obj_begin + sizeof( v ) };
    auto const data_addr{ reinterpret_cast<std::uintptr_t>( v.data() ) };
    EXPECT_GE( data_addr, obj_begin );
    EXPECT_LT( data_addr, obj_end   );
}

TEST( small_vector_compact, inline_to_heap_transition )
{
    msb_small_vector<int, 4> v;
    for ( int i{ 0 }; i < 4; ++i )
        v.push_back( i );

    // Still inline
    auto const obj_begin{ reinterpret_cast<std::uintptr_t>( &v ) };
    auto const obj_end  { obj_begin + sizeof( v ) };
    auto data_addr{ reinterpret_cast<std::uintptr_t>( v.data() ) };
    EXPECT_GE( data_addr, obj_begin );
    EXPECT_LT( data_addr, obj_end   );

    // Push one more — triggers heap transition
    v.push_back( 4 );
    data_addr = reinterpret_cast<std::uintptr_t>( v.data() );
    EXPECT_TRUE( data_addr < obj_begin || data_addr >= obj_end );

    // Verify all values survived
    for ( int i{ 0 }; i < 5; ++i )
        EXPECT_EQ( v[ i ], i );
}

TEST( small_vector_compact, move_from_inline )
{
    msb_small_vector<int, 8> src;
    for ( int i{ 0 }; i < 4; ++i )
        src.push_back( i * 10 );

    msb_small_vector<int, 8> dst{ std::move( src ) };
    EXPECT_EQ( dst.size(), 4u );
    EXPECT_EQ( src.size(), 0u );
    for ( int i{ 0 }; i < 4; ++i )
        EXPECT_EQ( dst[ i ], i * 10 );
}

TEST( small_vector_compact, move_from_heap )
{
    msb_small_vector<int, 2> src;
    for ( int i{ 0 }; i < 10; ++i )
        src.push_back( i );

    auto const heap_ptr{ src.data() };
    msb_small_vector<int, 2> dst{ std::move( src ) };
    EXPECT_EQ( dst.size(), 10u );
    EXPECT_EQ( src.size(), 0u );
    // Should have stolen the pointer
    EXPECT_EQ( dst.data(), heap_ptr );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( dst[ i ], i );
}

TEST( small_vector_compact, copy_from_inline )
{
    msb_small_vector<int, 8> src;
    for ( int i{ 0 }; i < 4; ++i )
        src.push_back( i );

    msb_small_vector<int, 8> dst{ src };
    EXPECT_EQ( dst.size(), 4u );
    EXPECT_EQ( src.size(), 4u );
    EXPECT_NE( dst.data(), src.data() );
    for ( int i{ 0 }; i < 4; ++i )
        EXPECT_EQ( dst[ i ], src[ i ] );
}

TEST( small_vector_compact, copy_from_heap )
{
    msb_small_vector<int, 2> src;
    for ( int i{ 0 }; i < 10; ++i )
        src.push_back( i );

    msb_small_vector<int, 2> dst{ src };
    EXPECT_EQ( dst.size(), 10u );
    EXPECT_NE( dst.data(), src.data() );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( dst[ i ], src[ i ] );
}

TEST( small_vector_compact, move_assign_inline_to_inline )
{
    msb_small_vector<int, 8> a{ 1, 2, 3 };
    msb_small_vector<int, 8> b{ 10, 20 };
    b = std::move( a );
    EXPECT_EQ( b.size(), 3u );
    EXPECT_EQ( b[ 0 ], 1 );
    EXPECT_EQ( a.size(), 0u );
}

TEST( small_vector_compact, move_assign_heap_to_inline )
{
    msb_small_vector<int, 2> a;
    for ( int i{ 0 }; i < 10; ++i )
        a.push_back( i );

    msb_small_vector<int, 2> b{ 1 };
    b = std::move( a );
    EXPECT_EQ( b.size(), 10u );
    EXPECT_EQ( a.size(), 0u );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( b[ i ], i );
}

TEST( small_vector_compact, move_assign_inline_to_heap )
{
    msb_small_vector<int, 4> a{ 1, 2 };
    msb_small_vector<int, 4> b;
    for ( int i{ 0 }; i < 10; ++i )
        b.push_back( i );

    b = std::move( a );
    EXPECT_EQ( b.size(), 2u );
    EXPECT_EQ( b[ 0 ], 1 );
    EXPECT_EQ( a.size(), 0u );
}

TEST( small_vector_compact, move_assign_heap_to_heap )
{
    msb_small_vector<int, 2> a;
    msb_small_vector<int, 2> b;
    for ( int i{ 0 }; i < 10; ++i )
        a.push_back( i );
    for ( int i{ 0 }; i < 5; ++i )
        b.push_back( i * 100 );

    b = std::move( a );
    EXPECT_EQ( b.size(), 10u );
    EXPECT_EQ( a.size(), 0u );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( b[ i ], i );
}

TEST( small_vector_compact, erase_if_free_function )
{
    msb_small_vector<int, 8> v{ 1, 2, 3, 4, 5, 6 };
    auto const removed{ erase_if( v, []( int x ) { return x % 2 == 0; } ) };
    EXPECT_EQ( removed, 3u );
    EXPECT_EQ( v.size(), 3u );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 3 );
    EXPECT_EQ( v[ 2 ], 5 );
}

TEST( small_vector_compact, erase_free_function )
{
    msb_small_vector<int, 8> v{ 1, 2, 3, 2, 4, 2 };
    auto const removed{ erase( v, 2 ) };
    EXPECT_EQ( removed, 3u );
    EXPECT_EQ( v.size(), 3u );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 3 );
    EXPECT_EQ( v[ 2 ], 4 );
}

TEST( small_vector_compact, stress_push_clear_push )
{
    msb_small_vector<int, 4> v;
    for ( int i{ 0 }; i < 1000; ++i )
        v.push_back( i );
    EXPECT_EQ( v.size(), 1000u );

    v.clear();
    EXPECT_EQ( v.size(), 0u );

    for ( int i{ 0 }; i < 500; ++i )
        v.push_back( i * 2 );
    EXPECT_EQ( v.size(), 500u );
    EXPECT_EQ( v[ 0 ], 0 );
    EXPECT_EQ( v[ 499 ], 998 );
}

TEST( small_vector_compact, trivially_relocatable )
{
    static_assert(  is_trivially_moveable<msb_small_vector<int, 4>> );
    static_assert(  is_trivially_moveable<msb_small_vector<int, 16>> );
    static_assert( !is_trivially_moveable<pb_small_vector<int, 4>> );
}

TEST( small_vector_compact, reserve_inline )
{
    msb_small_vector<int, 8> v;
    v.reserve( 4 ); // within inline capacity — should stay inline
    EXPECT_GE( v.capacity(), 4u );

    auto const obj_begin{ reinterpret_cast<std::uintptr_t>( &v ) };
    auto const obj_end  { obj_begin + sizeof( v ) };
    auto const data_addr{ reinterpret_cast<std::uintptr_t>( v.data() ) };
    EXPECT_GE( data_addr, obj_begin );
    EXPECT_LT( data_addr, obj_end   );
}

TEST( small_vector_compact, reserve_heap )
{
    msb_small_vector<int, 4> v;
    v.reserve( 100 ); // exceeds inline — should go to heap
    EXPECT_GE( v.capacity(), 100u );
    EXPECT_EQ( v.size(), 0u );

    for ( int i{ 0 }; i < 100; ++i )
        v.push_back( i );
    EXPECT_EQ( v.size(), 100u );
}

////////////////////////////////////////////////////////////////////////////////
// Pointer-based layout tests
////////////////////////////////////////////////////////////////////////////////

TEST( small_vector_pointer, inline_storage )
{
    pb_small_vector<int, 4> v;
    v.push_back( 1 );
    v.push_back( 2 );
    v.push_back( 3 );

    EXPECT_TRUE( v.is_small() );
    auto const obj_begin{ reinterpret_cast<std::uintptr_t>( &v ) };
    auto const obj_end  { obj_begin + sizeof( v ) };
    auto const data_addr{ reinterpret_cast<std::uintptr_t>( v.data() ) };
    EXPECT_GE( data_addr, obj_begin );
    EXPECT_LT( data_addr, obj_end   );
}

TEST( small_vector_pointer, inline_to_heap_transition )
{
    pb_small_vector<int, 4> v;
    for ( int i{ 0 }; i < 4; ++i )
        v.push_back( i );

    EXPECT_TRUE( v.is_small() );

    // Push one more — triggers heap transition
    v.push_back( 4 );
    EXPECT_FALSE( v.is_small() );

    auto const obj_begin{ reinterpret_cast<std::uintptr_t>( &v ) };
    auto const obj_end  { obj_begin + sizeof( v ) };
    auto const data_addr{ reinterpret_cast<std::uintptr_t>( v.data() ) };
    EXPECT_TRUE( data_addr < obj_begin || data_addr >= obj_end );

    for ( int i{ 0 }; i < 5; ++i )
        EXPECT_EQ( v[ i ], i );
}

TEST( small_vector_pointer, move_from_inline )
{
    pb_small_vector<int, 8> src;
    for ( int i{ 0 }; i < 4; ++i )
        src.push_back( i * 10 );

    EXPECT_TRUE( src.is_small() );
    pb_small_vector<int, 8> dst{ std::move( src ) };
    EXPECT_EQ( dst.size(), 4u );
    EXPECT_EQ( src.size(), 0u );
    EXPECT_TRUE( dst.is_small() );
    EXPECT_TRUE( src.is_small() );
    for ( int i{ 0 }; i < 4; ++i )
        EXPECT_EQ( dst[ i ], i * 10 );
}

TEST( small_vector_pointer, move_from_heap )
{
    pb_small_vector<int, 2> src;
    for ( int i{ 0 }; i < 10; ++i )
        src.push_back( i );

    EXPECT_FALSE( src.is_small() );
    auto const heap_ptr{ src.data() };
    pb_small_vector<int, 2> dst{ std::move( src ) };
    EXPECT_EQ( dst.size(), 10u );
    EXPECT_EQ( src.size(), 0u );
    EXPECT_FALSE( dst.is_small() );
    EXPECT_TRUE ( src.is_small() );
    // Should have stolen the pointer
    EXPECT_EQ( dst.data(), heap_ptr );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( dst[ i ], i );
}

TEST( small_vector_pointer, copy_from_inline )
{
    pb_small_vector<int, 8> src;
    for ( int i{ 0 }; i < 4; ++i )
        src.push_back( i );

    pb_small_vector<int, 8> dst{ src };
    EXPECT_EQ( dst.size(), 4u );
    EXPECT_EQ( src.size(), 4u );
    EXPECT_TRUE( dst.is_small() );
    EXPECT_NE( dst.data(), src.data() );
    for ( int i{ 0 }; i < 4; ++i )
        EXPECT_EQ( dst[ i ], src[ i ] );
}

TEST( small_vector_pointer, copy_from_heap )
{
    pb_small_vector<int, 2> src;
    for ( int i{ 0 }; i < 10; ++i )
        src.push_back( i );

    pb_small_vector<int, 2> dst{ src };
    EXPECT_EQ( dst.size(), 10u );
    EXPECT_FALSE( dst.is_small() );
    EXPECT_NE( dst.data(), src.data() );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( dst[ i ], src[ i ] );
}

TEST( small_vector_pointer, move_assign_inline_to_inline )
{
    pb_small_vector<int, 8> a{ 1, 2, 3 };
    pb_small_vector<int, 8> b{ 10, 20 };
    b = std::move( a );
    EXPECT_EQ( b.size(), 3u );
    EXPECT_EQ( b[ 0 ], 1 );
    EXPECT_EQ( a.size(), 0u );
    EXPECT_TRUE( b.is_small() );
}

TEST( small_vector_pointer, move_assign_heap_to_inline )
{
    pb_small_vector<int, 2> a;
    for ( int i{ 0 }; i < 10; ++i )
        a.push_back( i );

    pb_small_vector<int, 2> b{ 1 };
    b = std::move( a );
    EXPECT_EQ( b.size(), 10u );
    EXPECT_EQ( a.size(), 0u );
    EXPECT_FALSE( b.is_small() );
    EXPECT_TRUE ( a.is_small() );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( b[ i ], i );
}

TEST( small_vector_pointer, move_assign_inline_to_heap )
{
    pb_small_vector<int, 4> a{ 1, 2 };
    pb_small_vector<int, 4> b;
    for ( int i{ 0 }; i < 10; ++i )
        b.push_back( i );

    b = std::move( a );
    EXPECT_EQ( b.size(), 2u );
    EXPECT_EQ( b[ 0 ], 1 );
    EXPECT_EQ( a.size(), 0u );
    EXPECT_TRUE( b.is_small() );
}

TEST( small_vector_pointer, move_assign_heap_to_heap )
{
    pb_small_vector<int, 2> a;
    pb_small_vector<int, 2> b;
    for ( int i{ 0 }; i < 10; ++i )
        a.push_back( i );
    for ( int i{ 0 }; i < 5; ++i )
        b.push_back( i * 100 );

    b = std::move( a );
    EXPECT_EQ( b.size(), 10u );
    EXPECT_EQ( a.size(), 0u );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( b[ i ], i );
}

TEST( small_vector_pointer, erase_if_free_function )
{
    pb_small_vector<int, 8> v{ 1, 2, 3, 4, 5, 6 };
    auto const removed{ erase_if( v, []( int x ) { return x % 2 == 0; } ) };
    EXPECT_EQ( removed, 3u );
    EXPECT_EQ( v.size(), 3u );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 3 );
    EXPECT_EQ( v[ 2 ], 5 );
}

TEST( small_vector_pointer, erase_free_function )
{
    pb_small_vector<int, 8> v{ 1, 2, 3, 2, 4, 2 };
    auto const removed{ erase( v, 2 ) };
    EXPECT_EQ( removed, 3u );
    EXPECT_EQ( v.size(), 3u );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 3 );
    EXPECT_EQ( v[ 2 ], 4 );
}

TEST( small_vector_pointer, stress_push_clear_push )
{
    pb_small_vector<int, 4> v;
    for ( int i{ 0 }; i < 1000; ++i )
        v.push_back( i );
    EXPECT_EQ( v.size(), 1000u );
    EXPECT_FALSE( v.is_small() );

    v.clear();
    EXPECT_EQ( v.size(), 0u );

    for ( int i{ 0 }; i < 500; ++i )
        v.push_back( i * 2 );
    EXPECT_EQ( v.size(), 500u );
    EXPECT_EQ( v[ 0 ], 0 );
    EXPECT_EQ( v[ 499 ], 998 );
}

TEST( small_vector_pointer, trivially_relocatable )
{
    static_assert( !is_trivially_moveable<pb_small_vector<int, 4>> );
    static_assert( !is_trivially_moveable<pb_small_vector<int, 16>> );
}

TEST( small_vector_pointer, reserve_inline )
{
    pb_small_vector<int, 8> v;
    v.reserve( 4 ); // within inline capacity — should stay inline
    EXPECT_GE( v.capacity(), 4u );
    EXPECT_TRUE( v.is_small() );
}

TEST( small_vector_pointer, reserve_heap )
{
    pb_small_vector<int, 4> v;
    v.reserve( 100 );
    EXPECT_GE( v.capacity(), 100u );
    EXPECT_EQ( v.size(), 0u );
    EXPECT_FALSE( v.is_small() );

    for ( int i{ 0 }; i < 100; ++i )
        v.push_back( i );
    EXPECT_EQ( v.size(), 100u );
}

TEST( small_vector_pointer, type_erasure_via_base )
{
    pb_small_vector<int, 4>  a{ 1, 2, 3 };
    pb_small_vector<int, 16> b{ 10, 20 };

    // Both can be referenced through the same base type
    using base_t = small_vector_base<int, std::size_t, pb_opts>;
    base_t & a_ref{ a };
    base_t & b_ref{ b };

    EXPECT_EQ( a_ref.size(), 3u );
    EXPECT_EQ( b_ref.size(), 2u );
    EXPECT_EQ( a_ref[ 0 ], 1 );
    EXPECT_EQ( b_ref[ 0 ], 10 );

    // Can push through base reference
    a_ref.push_back( 4 );
    EXPECT_EQ( a_ref.size(), 4u );
    EXPECT_EQ( a[ 3 ], 4 );
}

////////////////////////////////////////////////////////////////////////////////
// Compact LSB layout tests
////////////////////////////////////////////////////////////////////////////////

TEST( small_vector_compact_lsb, inline_storage )
{
    lsb_small_vector<int, 4> v;
    v.push_back( 1 );
    v.push_back( 2 );
    v.push_back( 3 );

    auto const obj_begin{ reinterpret_cast<std::uintptr_t>( &v ) };
    auto const obj_end  { obj_begin + sizeof( v ) };
    auto const data_addr{ reinterpret_cast<std::uintptr_t>( v.data() ) };
    EXPECT_GE( data_addr, obj_begin );
    EXPECT_LT( data_addr, obj_end   );
}

TEST( small_vector_compact_lsb, inline_to_heap_transition )
{
    lsb_small_vector<int, 4> v;
    for ( int i{ 0 }; i < 4; ++i )
        v.push_back( i );

    auto const obj_begin{ reinterpret_cast<std::uintptr_t>( &v ) };
    auto const obj_end  { obj_begin + sizeof( v ) };
    auto data_addr{ reinterpret_cast<std::uintptr_t>( v.data() ) };
    EXPECT_GE( data_addr, obj_begin );
    EXPECT_LT( data_addr, obj_end   );

    v.push_back( 4 );
    data_addr = reinterpret_cast<std::uintptr_t>( v.data() );
    EXPECT_TRUE( data_addr < obj_begin || data_addr >= obj_end );

    for ( int i{ 0 }; i < 5; ++i )
        EXPECT_EQ( v[ i ], i );
}

TEST( small_vector_compact_lsb, move_from_inline )
{
    lsb_small_vector<int, 8> src;
    for ( int i{ 0 }; i < 4; ++i )
        src.push_back( i * 10 );

    lsb_small_vector<int, 8> dst{ std::move( src ) };
    EXPECT_EQ( dst.size(), 4u );
    EXPECT_EQ( src.size(), 0u );
    for ( int i{ 0 }; i < 4; ++i )
        EXPECT_EQ( dst[ i ], i * 10 );
}

TEST( small_vector_compact_lsb, move_from_heap )
{
    lsb_small_vector<int, 2> src;
    for ( int i{ 0 }; i < 10; ++i )
        src.push_back( i );

    auto const heap_ptr{ src.data() };
    lsb_small_vector<int, 2> dst{ std::move( src ) };
    EXPECT_EQ( dst.size(), 10u );
    EXPECT_EQ( src.size(), 0u );
    EXPECT_EQ( dst.data(), heap_ptr );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( dst[ i ], i );
}

TEST( small_vector_compact_lsb, copy_from_inline )
{
    lsb_small_vector<int, 8> src;
    for ( int i{ 0 }; i < 4; ++i )
        src.push_back( i );

    lsb_small_vector<int, 8> dst{ src };
    EXPECT_EQ( dst.size(), 4u );
    EXPECT_EQ( src.size(), 4u );
    EXPECT_NE( dst.data(), src.data() );
    for ( int i{ 0 }; i < 4; ++i )
        EXPECT_EQ( dst[ i ], src[ i ] );
}

TEST( small_vector_compact_lsb, copy_from_heap )
{
    lsb_small_vector<int, 2> src;
    for ( int i{ 0 }; i < 10; ++i )
        src.push_back( i );

    lsb_small_vector<int, 2> dst{ src };
    EXPECT_EQ( dst.size(), 10u );
    EXPECT_NE( dst.data(), src.data() );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( dst[ i ], src[ i ] );
}

TEST( small_vector_compact_lsb, move_assign_inline_to_inline )
{
    lsb_small_vector<int, 8> a{ 1, 2, 3 };
    lsb_small_vector<int, 8> b{ 10, 20 };
    b = std::move( a );
    EXPECT_EQ( b.size(), 3u );
    EXPECT_EQ( b[ 0 ], 1 );
    EXPECT_EQ( a.size(), 0u );
}

TEST( small_vector_compact_lsb, move_assign_heap_to_inline )
{
    lsb_small_vector<int, 2> a;
    for ( int i{ 0 }; i < 10; ++i )
        a.push_back( i );

    lsb_small_vector<int, 2> b{ 1 };
    b = std::move( a );
    EXPECT_EQ( b.size(), 10u );
    EXPECT_EQ( a.size(), 0u );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( b[ i ], i );
}

TEST( small_vector_compact_lsb, move_assign_inline_to_heap )
{
    lsb_small_vector<int, 4> a{ 1, 2 };
    lsb_small_vector<int, 4> b;
    for ( int i{ 0 }; i < 10; ++i )
        b.push_back( i );

    b = std::move( a );
    EXPECT_EQ( b.size(), 2u );
    EXPECT_EQ( b[ 0 ], 1 );
    EXPECT_EQ( a.size(), 0u );
}

TEST( small_vector_compact_lsb, move_assign_heap_to_heap )
{
    lsb_small_vector<int, 2> a;
    lsb_small_vector<int, 2> b;
    for ( int i{ 0 }; i < 10; ++i )
        a.push_back( i );
    for ( int i{ 0 }; i < 5; ++i )
        b.push_back( i * 100 );

    b = std::move( a );
    EXPECT_EQ( b.size(), 10u );
    EXPECT_EQ( a.size(), 0u );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( b[ i ], i );
}

TEST( small_vector_compact_lsb, erase_if_free_function )
{
    lsb_small_vector<int, 8> v{ 1, 2, 3, 4, 5, 6 };
    auto const removed{ erase_if( v, []( int x ) { return x % 2 == 0; } ) };
    EXPECT_EQ( removed, 3u );
    EXPECT_EQ( v.size(), 3u );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 3 );
    EXPECT_EQ( v[ 2 ], 5 );
}

TEST( small_vector_compact_lsb, erase_free_function )
{
    lsb_small_vector<int, 8> v{ 1, 2, 3, 2, 4, 2 };
    auto const removed{ erase( v, 2 ) };
    EXPECT_EQ( removed, 3u );
    EXPECT_EQ( v.size(), 3u );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 3 );
    EXPECT_EQ( v[ 2 ], 4 );
}

TEST( small_vector_compact_lsb, stress_push_clear_push )
{
    lsb_small_vector<int, 4> v;
    for ( int i{ 0 }; i < 1000; ++i )
        v.push_back( i );
    EXPECT_EQ( v.size(), 1000u );

    v.clear();
    EXPECT_EQ( v.size(), 0u );

    for ( int i{ 0 }; i < 500; ++i )
        v.push_back( i * 2 );
    EXPECT_EQ( v.size(), 500u );
    EXPECT_EQ( v[ 0 ], 0 );
    EXPECT_EQ( v[ 499 ], 998 );
}

TEST( small_vector_compact_lsb, trivially_relocatable )
{
    static_assert(  is_trivially_moveable<lsb_small_vector<int, 4>> );
    static_assert(  is_trivially_moveable<lsb_small_vector<int, 16>> );
    static_assert(  is_trivially_moveable<small_vector<int, 4, std::uint32_t, lsb_opts>> );
}

TEST( small_vector_compact_lsb, reserve_inline )
{
    lsb_small_vector<int, 8> v;
    v.reserve( 4 );
    EXPECT_GE( v.capacity(), 4u );

    auto const obj_begin{ reinterpret_cast<std::uintptr_t>( &v ) };
    auto const obj_end  { obj_begin + sizeof( v ) };
    auto const data_addr{ reinterpret_cast<std::uintptr_t>( v.data() ) };
    EXPECT_GE( data_addr, obj_begin );
    EXPECT_LT( data_addr, obj_end   );
}

TEST( small_vector_compact_lsb, reserve_heap )
{
    lsb_small_vector<int, 4> v;
    v.reserve( 100 );
    EXPECT_GE( v.capacity(), 100u );
    EXPECT_EQ( v.size(), 0u );

    for ( int i{ 0 }; i < 100; ++i )
        v.push_back( i );
    EXPECT_EQ( v.size(), 100u );
}

////////////////////////////////////////////////////////////////////////////////
// Embedded layout tests
////////////////////////////////////////////////////////////////////////////////

TEST( small_vector_embedded, inline_storage )
{
    emb_small_vector<int, 4> v;
    v.push_back( 1 );
    v.push_back( 2 );
    v.push_back( 3 );

    auto const obj_begin{ reinterpret_cast<std::uintptr_t>( &v ) };
    auto const obj_end  { obj_begin + sizeof( v ) };
    auto const data_addr{ reinterpret_cast<std::uintptr_t>( v.data() ) };
    EXPECT_GE( data_addr, obj_begin );
    EXPECT_LT( data_addr, obj_end   );
}

TEST( small_vector_embedded, inline_to_heap_transition )
{
    emb_small_vector<int, 4> v;
    for ( int i{ 0 }; i < 4; ++i )
        v.push_back( i );

    auto const obj_begin{ reinterpret_cast<std::uintptr_t>( &v ) };
    auto const obj_end  { obj_begin + sizeof( v ) };
    auto data_addr{ reinterpret_cast<std::uintptr_t>( v.data() ) };
    EXPECT_GE( data_addr, obj_begin );
    EXPECT_LT( data_addr, obj_end   );

    v.push_back( 4 );
    data_addr = reinterpret_cast<std::uintptr_t>( v.data() );
    EXPECT_TRUE( data_addr < obj_begin || data_addr >= obj_end );

    for ( int i{ 0 }; i < 5; ++i )
        EXPECT_EQ( v[ i ], i );
}

TEST( small_vector_embedded, move_from_inline )
{
    emb_small_vector<int, 8> src;
    for ( int i{ 0 }; i < 4; ++i )
        src.push_back( i * 10 );

    emb_small_vector<int, 8> dst{ std::move( src ) };
    EXPECT_EQ( dst.size(), 4u );
    EXPECT_EQ( src.size(), 0u );
    for ( int i{ 0 }; i < 4; ++i )
        EXPECT_EQ( dst[ i ], i * 10 );
}

TEST( small_vector_embedded, move_from_heap )
{
    emb_small_vector<int, 2> src;
    for ( int i{ 0 }; i < 10; ++i )
        src.push_back( i );

    auto const heap_ptr{ src.data() };
    emb_small_vector<int, 2> dst{ std::move( src ) };
    EXPECT_EQ( dst.size(), 10u );
    EXPECT_EQ( src.size(), 0u );
    EXPECT_EQ( dst.data(), heap_ptr );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( dst[ i ], i );
}

TEST( small_vector_embedded, copy_from_inline )
{
    emb_small_vector<int, 8> src;
    for ( int i{ 0 }; i < 4; ++i )
        src.push_back( i );

    emb_small_vector<int, 8> dst{ src };
    EXPECT_EQ( dst.size(), 4u );
    EXPECT_EQ( src.size(), 4u );
    EXPECT_NE( dst.data(), src.data() );
    for ( int i{ 0 }; i < 4; ++i )
        EXPECT_EQ( dst[ i ], src[ i ] );
}

TEST( small_vector_embedded, copy_from_heap )
{
    emb_small_vector<int, 2> src;
    for ( int i{ 0 }; i < 10; ++i )
        src.push_back( i );

    emb_small_vector<int, 2> dst{ src };
    EXPECT_EQ( dst.size(), 10u );
    EXPECT_NE( dst.data(), src.data() );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( dst[ i ], src[ i ] );
}

TEST( small_vector_embedded, move_assign_inline_to_inline )
{
    emb_small_vector<int, 8> a{ 1, 2, 3 };
    emb_small_vector<int, 8> b{ 10, 20 };
    b = std::move( a );
    EXPECT_EQ( b.size(), 3u );
    EXPECT_EQ( b[ 0 ], 1 );
    EXPECT_EQ( a.size(), 0u );
}

TEST( small_vector_embedded, move_assign_heap_to_inline )
{
    emb_small_vector<int, 2> a;
    for ( int i{ 0 }; i < 10; ++i )
        a.push_back( i );

    emb_small_vector<int, 2> b{ 1 };
    b = std::move( a );
    EXPECT_EQ( b.size(), 10u );
    EXPECT_EQ( a.size(), 0u );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( b[ i ], i );
}

TEST( small_vector_embedded, move_assign_inline_to_heap )
{
    emb_small_vector<int, 4> a{ 1, 2 };
    emb_small_vector<int, 4> b;
    for ( int i{ 0 }; i < 10; ++i )
        b.push_back( i );

    b = std::move( a );
    EXPECT_EQ( b.size(), 2u );
    EXPECT_EQ( b[ 0 ], 1 );
    EXPECT_EQ( a.size(), 0u );
}

TEST( small_vector_embedded, move_assign_heap_to_heap )
{
    emb_small_vector<int, 2> a;
    emb_small_vector<int, 2> b;
    for ( int i{ 0 }; i < 10; ++i )
        a.push_back( i );
    for ( int i{ 0 }; i < 5; ++i )
        b.push_back( i * 100 );

    b = std::move( a );
    EXPECT_EQ( b.size(), 10u );
    EXPECT_EQ( a.size(), 0u );
    for ( int i{ 0 }; i < 10; ++i )
        EXPECT_EQ( b[ i ], i );
}

TEST( small_vector_embedded, erase_if_free_function )
{
    emb_small_vector<int, 8> v{ 1, 2, 3, 4, 5, 6 };
    auto const removed{ erase_if( v, []( int x ) { return x % 2 == 0; } ) };
    EXPECT_EQ( removed, 3u );
    EXPECT_EQ( v.size(), 3u );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 3 );
    EXPECT_EQ( v[ 2 ], 5 );
}

TEST( small_vector_embedded, erase_free_function )
{
    emb_small_vector<int, 8> v{ 1, 2, 3, 2, 4, 2 };
    auto const removed{ erase( v, 2 ) };
    EXPECT_EQ( removed, 3u );
    EXPECT_EQ( v.size(), 3u );
    EXPECT_EQ( v[ 0 ], 1 );
    EXPECT_EQ( v[ 1 ], 3 );
    EXPECT_EQ( v[ 2 ], 4 );
}

TEST( small_vector_embedded, stress_push_clear_push )
{
    emb_small_vector<int, 4> v;
    for ( int i{ 0 }; i < 1000; ++i )
        v.push_back( i );
    EXPECT_EQ( v.size(), 1000u );

    v.clear();
    EXPECT_EQ( v.size(), 0u );

    for ( int i{ 0 }; i < 500; ++i )
        v.push_back( i * 2 );
    EXPECT_EQ( v.size(), 500u );
    EXPECT_EQ( v[ 0 ], 0 );
    EXPECT_EQ( v[ 499 ], 998 );
}

TEST( small_vector_embedded, trivially_relocatable )
{
    static_assert(  is_trivially_moveable<emb_small_vector<int, 4>> );
    static_assert(  is_trivially_moveable<emb_small_vector<int, 16>> );
    static_assert(  is_trivially_moveable<small_vector<int, 4, std::uint32_t, emb_opts>> );
}

TEST( small_vector_embedded, reserve_inline )
{
    emb_small_vector<int, 8> v;
    v.reserve( 4 );
    EXPECT_GE( v.capacity(), 4u );

    auto const obj_begin{ reinterpret_cast<std::uintptr_t>( &v ) };
    auto const obj_end  { obj_begin + sizeof( v ) };
    auto const data_addr{ reinterpret_cast<std::uintptr_t>( v.data() ) };
    EXPECT_GE( data_addr, obj_begin );
    EXPECT_LT( data_addr, obj_end   );
}

TEST( small_vector_embedded, reserve_heap )
{
    emb_small_vector<int, 4> v;
    v.reserve( 100 );
    EXPECT_GE( v.capacity(), 100u );
    EXPECT_EQ( v.size(), 0u );

    for ( int i{ 0 }; i < 100; ++i )
        v.push_back( i );
    EXPECT_EQ( v.size(), 100u );
}

TEST( small_vector_embedded, sizeof_no_worse_than_compact_lsb )
{
    // embedded stores size inside the union (common initial sequence) →
    // no external size_ field. Should never be larger than compact_lsb.
    using emb_sv = emb_small_vector<int, 4>;
    using lsb_sv = lsb_small_vector<int, 4>;
    EXPECT_LE( sizeof( emb_sv ), sizeof( lsb_sv ) );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
