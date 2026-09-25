////////////////////////////////////////////////////////////////////////////////
/// psi::vm sort utilities test suite
///
/// Every entry point is checked for exact agreement with the std:: algorithm
/// that defines its result, over the input shapes that steer pdqsort and
/// spreadsort down different paths (random, sorted, reverse sorted, few
/// distinct values, sorted runs, all equal, and the extreme values 0 and the
/// maximum mixed into random ones) and over sizes around each algorithm's own
/// thresholds.
////////////////////////////////////////////////////////////////////////////////

#include <psi/vm/sort_keys.hpp>
#include <psi/vm/sort_keys_radix.hpp>
#include <psi/vm/containers/heap_vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <compare>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <ranges>
#include <span>
#include <vector>

//------------------------------------------------------------------------------
namespace psi::vm {
//------------------------------------------------------------------------------

namespace
{
    // A strong typedef around a 32-bit unsigned integer.
    struct strong_u32
    {
        std::uint32_t value;

        static constexpr bool orders_as_unsigned{ true };

        friend constexpr auto operator<=>( strong_u32, strong_u32 ) noexcept = default;
    };

    // Two 32-bit halves ordered by (high, low), laid out so that the 64-bit
    // little-endian reading of the pair is high << 32 | low. Its fields give
    // it the alignment of a 32-bit integer only, so it declares that of the
    // 64-bit one it is sorted as.
    struct alignas( std::uint64_t ) high_low_pair
    {
        std::uint32_t low;
        std::uint32_t high;

        static constexpr bool orders_as_unsigned{ std::endian::native == std::endian::little };

        friend constexpr std::strong_ordering operator<=>( high_low_pair const l, high_low_pair const r ) noexcept
        {
            if ( auto const c{ l.high <=> r.high }; c != 0 )
                return c;
            return l.low <=> r.low;
        }
    };

    // A signed 32-bit integer held with its sign bit flipped, so that its
    // unsigned reading orders as the integer does (the recipe of sort_key).
    struct biased_i32
    {
        std::uint32_t bits;

        static constexpr bool orders_as_unsigned{ true };

        constexpr std::int32_t value() const noexcept { return static_cast<std::int32_t>( bits ^ 0x8000'0000U ); }

        friend constexpr std::strong_ordering operator<=>( biased_i32 const l, biased_i32 const r ) noexcept { return l.value() <=> r.value(); }
    };

    // An unsigned integral type of a key's width that is not the worker's own
    // integer type (unsigned long long where std::uint64_t is unsigned long,
    // unsigned long otherwise), so that it takes the storage-reuse path.
    using other_unsigned = std::conditional_t<std::is_same_v<unsigned long, std::uint64_t>, unsigned long long, unsigned long>;

    struct no_opt_in { std::uint32_t value; };
    // aligned as the integer it would be read as, so that only its padding disqualifies it
    struct alignas( std::uint64_t ) padded { std::uint8_t a; std::uint32_t b; static constexpr bool orders_as_unsigned{ true }; };

    // Eight bytes of Field-wide fields, aligned to Align.
    template <typename Field, std::size_t Align>
    struct alignas( Align ) eight_bytes_of
    {
        Field fields[ sizeof( std::uint64_t ) / sizeof( Field ) ];

        static constexpr bool orders_as_unsigned{ true };
    };

    static_assert(  sort_key<std::uint32_t> );
    static_assert(  sort_key<std::uint64_t> );
    static_assert(  sort_key<strong_u32   > );
    static_assert(  sort_key<high_low_pair> == ( std::endian::native == std::endian::little ) );
    static_assert( !sort_key<std::int32_t > );              // signed: its unsigned reading does not order as it does
    static_assert( !sort_key<std::uint16_t> );              // no worker of that width
    static_assert( !sort_key<float        > );
    static_assert( !sort_key<no_opt_in    > );              // says nothing about its representation
    static_assert( !sort_key<padded       > );              // padding bits carry no order
    static_assert( !sort_key<std::uint32_t const> );
    static_assert(  sort_key<other_unsigned> && !std::is_same_v<other_unsigned, key_sort_detail::key_uint_t<other_unsigned>> );

    // The sort creates 64-bit integers in the storage of 8-byte keys, so a key
    // of narrower fields is one only once it declares their alignment.
    static_assert( !sort_key<eight_bytes_of<std::uint8_t , 1>> );
    static_assert( !sort_key<eight_bytes_of<std::uint16_t, 2>> );
    static_assert( !sort_key<eight_bytes_of<std::uint32_t, 4>> );
    static_assert(  sort_key<eight_bytes_of<std::uint8_t , alignof( std::uint64_t )>> );
    static_assert(  sort_key<eight_bytes_of<std::uint16_t, alignof( std::uint64_t )>> );
    static_assert(  sort_key<eight_bytes_of<std::uint32_t, alignof( std::uint64_t )>> );

    // pdq never allocates; radix does, so it is noexcept only where the build
    // states that an allocation cannot fail.
    static_assert( noexcept( sort_keys( std::span<std::uint32_t>{} ) ) );
    static_assert( noexcept( sort_keys( std::span<strong_u32   >{} ) ) );
    static_assert( noexcept( sort_unique_keys( std::span<std::uint64_t>{} ) ) );
#if PSI_VM_HAS_INTEGER_SORT
    static_assert( noexcept( sort_keys<key_sort_algo::radix>( std::span<std::uint64_t>{} ) ) == key_sort_detail::allocation_nothrow );
#endif

    template <typename T>
    T make_key( std::uint64_t const bits ) noexcept
    {
        if constexpr ( std::is_integral_v<T> )
            return static_cast<T>( bits );
        else
            return std::bit_cast<T>( static_cast<key_sort_detail::key_uint_t<T>>( bits ) );
    }

    enum struct shape { random, sorted, reverse, few_distinct, runs, all_equal, extremes };
    constexpr shape all_shapes[]{ shape::random, shape::sorted, shape::reverse, shape::few_distinct, shape::runs, shape::all_equal, shape::extremes };

    std::uint64_t key_bits( shape const s, std::mt19937_64 & rng ) noexcept
    {
        auto constexpr golden{ 0x9E3779B97F4A7C15ULL };
        switch ( s )
        {
            case shape::few_distinct: return rng() % 7 * golden;
            case shape::all_equal   : return golden;
            case shape::extremes    :
            {
                // 0 and the maximum of every width, among random keys
                auto const bits{ rng() };
                switch ( bits % 3 )
                {
                    case 0 : return 0;
                    case 1 : return std::numeric_limits<std::uint64_t>::max();
                    default: return bits;
                }
            }
            default: return rng();
        }
    }

    // Keys spread over the whole width, so that both halves of a two-field key
    // take part in the order.
    template <typename T>
    std::vector<T> make_input( shape const s, std::size_t const n, std::uint64_t const seed )
    {
        std::mt19937_64 rng{ seed };
        std::vector<T> keys;
        keys.reserve( n );
        for ( std::size_t i{ 0 }; i < n; ++i )
            keys.push_back( make_key<T>( key_bits( s, rng ) ) );
        switch ( s )
        {
            case shape::random      :
            case shape::few_distinct:
            case shape::all_equal   :
            case shape::extremes    : break;
            case shape::sorted      : std::sort( keys.begin(), keys.end() ); break;
            case shape::reverse     : std::sort( keys.begin(), keys.end(), []( T const & l, T const & r ) { return r < l; } ); break;
            case shape::runs        :
                for ( std::size_t run{ 0 }; run < n; run += 37 )
                    std::sort( keys.begin() + static_cast<std::ptrdiff_t>( run ), keys.begin() + static_cast<std::ptrdiff_t>( std::min( n, run + 37 ) ) );
                break;
        }
        return keys;
    }

    constexpr std::size_t sizes[]{ 0, 1, 2, 63, 64, 999, 1000, 1001, 100'000 };

    template <typename T>
    bool equivalent( T const & l, T const & r ) noexcept { return !( l < r ) && !( r < l ); }

    template <std::ranges::contiguous_range A, std::ranges::contiguous_range B>
    requires std::same_as<std::ranges::range_value_t<A>, std::ranges::range_value_t<B>>
    bool same_bytes( A const & a, B const & b ) noexcept
    {
        auto const size{ std::ranges::size( a ) };
        return size == std::ranges::size( b ) && ( size == 0 || std::memcmp( std::ranges::data( a ), std::ranges::data( b ), size * sizeof( std::ranges::range_value_t<A> ) ) == 0 );
    }
} // anonymous namespace

template <typename T>
class sort_keys_typed : public ::testing::Test {};

using key_types = ::testing::Types<std::uint32_t, std::uint64_t, other_unsigned, strong_u32, high_low_pair, biased_i32>;
TYPED_TEST_SUITE( sort_keys_typed, key_types );

template <key_sort_algo Algo, typename T>
void check_sort_keys()
{
    std::uint64_t seed{ 1 };
    for ( auto const s : all_shapes )
    for ( auto const n : sizes )
    {
        auto       actual  { make_input<T>( s, n, ++seed ) };
        auto       expected{ actual };
        std::stable_sort( expected.begin(), expected.end() );
        sort_keys<Algo>( std::span{ actual } );
        EXPECT_TRUE( same_bytes( actual, expected ) ) << "shape " << static_cast<int>( s ) << ", n " << n;
    }
}

TYPED_TEST( sort_keys_typed, pdq_agrees_with_std_stable_sort )
{
    if constexpr ( sort_key<TypeParam> )
        check_sort_keys<key_sort_algo::pdq, TypeParam>();
}

#if PSI_VM_HAS_INTEGER_SORT
TYPED_TEST( sort_keys_typed, radix_agrees_with_std_stable_sort )
{
    if constexpr ( sort_key<TypeParam> )
        check_sort_keys<key_sort_algo::radix, TypeParam>();
}
#endif

template <key_sort_algo Algo, typename T>
void check_sort_unique_keys()
{
    std::uint64_t seed{ 1'000 };
    for ( auto const s : all_shapes )
    for ( auto const n : sizes )
    {
        auto const input{ make_input<T>( s, n, ++seed ) };

        auto expected{ input };
        std::sort( expected.begin(), expected.end() );
        expected.erase( std::unique( expected.begin(), expected.end(), equivalent<T> ), expected.end() );
        if ( s == shape::all_equal )
        {
            EXPECT_EQ( expected.size(), std::min<std::size_t>( n, 1 ) );
        }

        auto actual{ input };
        auto const kept{ sort_unique_keys<Algo>( std::span{ actual } ) };
        EXPECT_EQ( kept, expected.size() ) << "shape " << static_cast<int>( s ) << ", n " << n;
        actual.resize( kept );
        EXPECT_TRUE( same_bytes( actual, expected ) ) << "shape " << static_cast<int>( s ) << ", n " << n;

        auto truncated{ input };
        sort_unique_keys<Algo>( truncated );
        EXPECT_TRUE( same_bytes( truncated, expected ) ) << "std::vector, shape " << static_cast<int>( s ) << ", n " << n;

        heap_vector<T, std::uint32_t> heap_truncated;
        heap_truncated.append_range( input );
        sort_unique_keys<Algo>( heap_truncated );
        EXPECT_TRUE( std::ranges::equal( heap_truncated, expected, equivalent<T> ) ) << "heap_vector, shape " << static_cast<int>( s ) << ", n " << n;
    }
}

TYPED_TEST( sort_keys_typed, pdq_unique_agrees_with_std_sort_and_unique )
{
    if constexpr ( sort_key<TypeParam> )
        check_sort_unique_keys<key_sort_algo::pdq, TypeParam>();
}

#if PSI_VM_HAS_INTEGER_SORT
TYPED_TEST( sort_keys_typed, radix_unique_agrees_with_std_sort_and_unique )
{
    if constexpr ( sort_key<TypeParam> )
        check_sort_unique_keys<key_sort_algo::radix, TypeParam>();
}
#endif

// Sorting part of an array leaves the keys on either side of it as they were.
template <key_sort_algo Algo, typename T>
void check_subspan()
{
    std::size_t constexpr margin{ 3 };
    std::uint64_t seed{ 3'000 };
    for ( auto const s : all_shapes )
    for ( auto const n : sizes )
    {
        auto const input { make_input<T>( s, n + 2 * margin, ++seed ) };
        auto const middle{ []( std::vector<T> & keys ) { return std::span{ keys }.subspan( margin, keys.size() - 2 * margin ); } };

        auto expected{ input };
        auto const sorted_middle{ middle( expected ) };
        std::stable_sort( sorted_middle.begin(), sorted_middle.end() );
        auto actual{ input };
        sort_keys<Algo>( middle( actual ) );
        EXPECT_TRUE( same_bytes( actual, expected ) ) << "shape " << static_cast<int>( s ) << ", n " << n;

        auto unique_expected{ input };
        auto const expected_middle{ middle( unique_expected ) };
        std::sort( expected_middle.begin(), expected_middle.end() );
        auto const expected_kept{ static_cast<std::size_t>( std::unique( expected_middle.begin(), expected_middle.end(), equivalent<T> ) - expected_middle.begin() ) };
        auto unique_actual{ input };
        auto const actual_middle{ middle( unique_actual ) };
        auto const kept{ sort_unique_keys<Algo>( actual_middle ) };
        EXPECT_EQ( kept, expected_kept ) << "unique, shape " << static_cast<int>( s ) << ", n " << n;
        EXPECT_TRUE( same_bytes( actual_middle.first( kept ), expected_middle.first( expected_kept ) ) ) << "unique, shape " << static_cast<int>( s ) << ", n " << n;
        EXPECT_TRUE( same_bytes( std::span{ unique_actual }.first( margin ), std::span{ input }.first( margin ) ) ) << "unique, shape " << static_cast<int>( s ) << ", n " << n;
        EXPECT_TRUE( same_bytes( std::span{ unique_actual }.last ( margin ), std::span{ input }.last ( margin ) ) ) << "unique, shape " << static_cast<int>( s ) << ", n " << n;
    }
}

TYPED_TEST( sort_keys_typed, pdq_of_a_subspan_leaves_its_neighbours_untouched )
{
    if constexpr ( sort_key<TypeParam> )
        check_subspan<key_sort_algo::pdq, TypeParam>();
}

#if PSI_VM_HAS_INTEGER_SORT
TYPED_TEST( sort_keys_typed, radix_of_a_subspan_leaves_its_neighbours_untouched )
{
    if constexpr ( sort_key<TypeParam> )
        check_subspan<key_sort_algo::radix, TypeParam>();
}
#endif

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
