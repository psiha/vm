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

#include <psi/vm/sort.hpp>
#include <psi/vm/sort_keys.hpp>
#include <psi/vm/sort_keys_radix.hpp>
#include <psi/vm/containers/flat_set.hpp>
#include <psi/vm/containers/heap_vector.hpp>
#include <psi/vm/containers/komparator.hpp>
#include <psi/vm/containers/small_vector.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <compare>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <numeric>
#include <random>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
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

    // A strong typedef around a 16-bit unsigned integer: a key narrower than
    // any it is packed with.
    struct strong_u16
    {
        std::uint16_t value;

        static constexpr bool orders_as_unsigned{ true };

        friend constexpr auto operator<=>( strong_u16, strong_u16 ) noexcept = default;
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

    // An IEEE floating-point value held biased as the sort_key recipe says -
    // every bit flipped when negative, only the sign bit when not - so that
    // its unsigned reading orders the values as std::strong_order does. It is
    // made from, and read back as, the value's bits: the tests build with
    // -ffast-math on some targets, where spelling an infinity, a NaN or -0.0
    // as a floating-point expression is not reliable.
    template <std::floating_point F, std::unsigned_integral U>
    struct alignas( U ) biased_float
    {
        static_assert( sizeof( F ) == sizeof( U ) && std::numeric_limits<F>::is_iec559 );
        static U constexpr sign    { U{ 1 } << ( sizeof( U ) * 8 - 1 ) };
        static U constexpr mantissa{ ( U{ 1 } << ( std::numeric_limits<F>::digits - 1 ) ) - 1 };
        static U constexpr infinity{ static_cast<U>( ~sign & ~mantissa ) };
        static U constexpr quiet   { infinity | ( ( mantissa + 1 ) >> 1 ) };

        U bits;

        static constexpr bool orders_as_unsigned{ true };

        static constexpr biased_float of( U const raw ) noexcept { return { static_cast<U>( ( raw & sign ) ? ~raw : ( raw ^ sign ) ) }; }
        constexpr U raw  () const noexcept { return static_cast<U>( ( bits & sign ) ? ( bits ^ sign ) : ~bits ); }
        constexpr F value() const noexcept { return std::bit_cast<F>( raw() ); }

        friend constexpr std::strong_ordering operator<=>( biased_float const l, biased_float const r ) noexcept { return std::strong_order( l.value(), r.value() ); }
        friend constexpr bool                 operator== ( biased_float const l, biased_float const r ) noexcept { return l.bits == r.bits; }
    };
    using biased_f32 = biased_float<float , std::uint32_t>;
    using biased_f64 = biased_float<double, std::uint64_t>;

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
    static_assert(  sort_key<std::uint8_t > );
    static_assert(  sort_key<std::uint16_t> );
    static_assert(  sort_key<strong_u16   > );
    struct three_bytes { std::uint8_t b[ 3 ]; static constexpr bool orders_as_unsigned{ true }; };
    static_assert( !sort_key<three_bytes  > );              // no unsigned integer of that width
    static_assert( !sort_key<float        > );
    static_assert( !sort_key<no_opt_in    > );              // says nothing about its representation
    static_assert( !sort_key<padded       > );              // padding bits carry no order
    static_assert( !sort_key<std::uint32_t const> );
    static_assert(  sort_key<other_unsigned> && !std::is_same_v<other_unsigned, key_sort_detail::key_uint_t<other_unsigned>> );
    static_assert(  sort_key<biased_f32> && sort_key<biased_f64> );

    // The order is one of the two that an unsigned reading describes, named as
    // std::sort's comparator is.
    static_assert(  key_order<std::less<>> && key_order<std::ranges::less> );
    static_assert(  key_order<std::greater<>> && key_order<std::ranges::greater> );
    static_assert( !key_order<std::greater<std::uint32_t>> );
    static_assert( !key_order<std::greater_equal<>> );
    static_assert( !key_order<decltype( []( std::uint32_t const l, std::uint32_t const r ) { return l > r; } )> );

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
    // argsort_keys gathers into scratch storage that spills to the heap, and
    // refuses a range of more keys than its index type counts, which only a
    // range with a wider size type can hold.
    static_assert( noexcept( argsort_keys( std::declval<heap_vector<std::uint32_t, std::uint32_t> const &>(), std::span<std::uint32_t>{} ) ) == key_sort_detail::allocation_nothrow );
    static_assert( noexcept( argsort_keys( std::span<std::uint32_t const>{}, std::span<std::uint32_t>{} ) ) == ( key_sort_detail::allocation_nothrow && sizeof( std::size_t ) == sizeof( std::uint32_t ) ) );
    static_assert( noexcept( argsort_keys<key_sort_algo::pdq, std::uint64_t>( std::span<std::uint32_t const>{}, std::span<std::uint64_t>{} ) ) == key_sort_detail::allocation_nothrow );
#if PSI_VM_HAS_INTEGER_SORT
    static_assert( noexcept( sort_keys<key_sort_algo::radix>( std::span<std::uint64_t>{} ) ) == key_sort_detail::allocation_nothrow );
#endif
    // and descending on the same terms
    static_assert( noexcept( sort_keys( std::span<strong_u32>{}, std::greater{} ) ) );
    static_assert( noexcept( sort_unique_keys( std::span<std::uint64_t>{}, std::ranges::greater{} ) ) );
    static_assert( noexcept( argsort_keys<key_sort_algo::pdq, std::uint64_t>( std::span<std::uint32_t const>{}, std::span<std::uint64_t>{}, std::greater{} ) ) == key_sort_detail::allocation_nothrow );

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

    // The reference comparator of an order, over the key's own operator<: its
    // reverse for std::greater - for the floats the reverse of std::strong_order.
    template <key_order Order>
    auto constexpr reference_order
    {
        []<typename T>( T const & l, T const & r ) noexcept -> bool
        {
            if constexpr ( key_sort_detail::is_descending<Order> ) return r < l;
            else                                                  return l < r;
        }
    };

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

using key_types = ::testing::Types<std::uint8_t, std::uint16_t, strong_u16, std::uint32_t, std::uint64_t, other_unsigned, strong_u32, high_low_pair, biased_i32, biased_f32, biased_f64>;
TYPED_TEST_SUITE( sort_keys_typed, key_types );

template <key_sort_algo Algo, typename T, key_order Order = std::less<>>
void check_sort_keys()
{
    std::uint64_t seed{ 1 };
    for ( auto const s : all_shapes )
    for ( auto const n : sizes )
    {
        auto       actual  { make_input<T>( s, n, ++seed ) };
        auto       expected{ actual };
        std::stable_sort( expected.begin(), expected.end(), reference_order<Order> );
        sort_keys<Algo>( std::span{ actual }, Order{} );
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

TYPED_TEST( sort_keys_typed, pdq_descending_agrees_with_std_stable_sort_reversed )
{
    if constexpr ( sort_key<TypeParam> )
        check_sort_keys<key_sort_algo::pdq, TypeParam, std::greater<>>();
}

#if PSI_VM_HAS_INTEGER_SORT
TYPED_TEST( sort_keys_typed, radix_descending_agrees_with_std_stable_sort_reversed )
{
    if constexpr ( sort_key<TypeParam> )
        check_sort_keys<key_sort_algo::radix, TypeParam, std::greater<>>();
}
#endif

template <key_sort_algo Algo, typename T, key_order Order = std::less<>>
void check_sort_unique_keys()
{
    std::uint64_t seed{ 1'000 };
    for ( auto const s : all_shapes )
    for ( auto const n : sizes )
    {
        auto const input{ make_input<T>( s, n, ++seed ) };

        auto expected{ input };
        std::sort( expected.begin(), expected.end(), reference_order<Order> );
        expected.erase( std::unique( expected.begin(), expected.end(), equivalent<T> ), expected.end() );
        if ( s == shape::all_equal )
        {
            EXPECT_EQ( expected.size(), std::min<std::size_t>( n, 1 ) );
        }

        auto actual{ input };
        auto const kept{ sort_unique_keys<Algo>( std::span{ actual }, Order{} ) };
        EXPECT_EQ( kept, expected.size() ) << "shape " << static_cast<int>( s ) << ", n " << n;
        actual.resize( kept );
        EXPECT_TRUE( same_bytes( actual, expected ) ) << "shape " << static_cast<int>( s ) << ", n " << n;

        auto truncated{ input };
        sort_unique_keys<Algo>( truncated, Order{} );
        EXPECT_TRUE( same_bytes( truncated, expected ) ) << "std::vector, shape " << static_cast<int>( s ) << ", n " << n;

        heap_vector<T, std::uint32_t> heap_truncated;
        heap_truncated.append_range( input );
        sort_unique_keys<Algo>( heap_truncated, Order{} );
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

TYPED_TEST( sort_keys_typed, pdq_unique_descending_agrees_with_std_sort_reversed_and_unique )
{
    if constexpr ( sort_key<TypeParam> )
        check_sort_unique_keys<key_sort_algo::pdq, TypeParam, std::greater<>>();
}

#if PSI_VM_HAS_INTEGER_SORT
TYPED_TEST( sort_keys_typed, radix_unique_descending_agrees_with_std_sort_reversed_and_unique )
{
    if constexpr ( sort_key<TypeParam> )
        check_sort_unique_keys<key_sort_algo::radix, TypeParam, std::greater<>>();
}
#endif

// Sorting part of an array leaves the keys on either side of it as they were.
template <key_sort_algo Algo, typename T, key_order Order = std::less<>>
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
        std::stable_sort( sorted_middle.begin(), sorted_middle.end(), reference_order<Order> );
        auto actual{ input };
        sort_keys<Algo>( middle( actual ), Order{} );
        EXPECT_TRUE( same_bytes( actual, expected ) ) << "shape " << static_cast<int>( s ) << ", n " << n;

        auto unique_expected{ input };
        auto const expected_middle{ middle( unique_expected ) };
        std::sort( expected_middle.begin(), expected_middle.end(), reference_order<Order> );
        auto const expected_kept{ static_cast<std::size_t>( std::unique( expected_middle.begin(), expected_middle.end(), equivalent<T> ) - expected_middle.begin() ) };
        auto unique_actual{ input };
        auto const actual_middle{ middle( unique_actual ) };
        auto const kept{ sort_unique_keys<Algo>( actual_middle, Order{} ) };
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

TYPED_TEST( sort_keys_typed, pdq_descending_of_a_subspan_leaves_its_neighbours_untouched )
{
    if constexpr ( sort_key<TypeParam> )
        check_subspan<key_sort_algo::pdq, TypeParam, std::greater<>>();
}

#if PSI_VM_HAS_INTEGER_SORT
TYPED_TEST( sort_keys_typed, radix_descending_of_a_subspan_leaves_its_neighbours_untouched )
{
    if constexpr ( sort_key<TypeParam> )
        check_subspan<key_sort_algo::radix, TypeParam, std::greater<>>();
}
#endif

template <key_sort_algo Algo, typename T, key_order Order = std::less<>>
void check_argsort()
{
    std::uint64_t seed{ 2'000 };
    for ( auto const s : all_shapes )
    for ( auto const n : sizes )
    {
        auto const keys{ make_input<T>( s, n, ++seed ) };

        std::vector<std::uint32_t> expected( n );
        std::iota( expected.begin(), expected.end(), 0U );
        std::stable_sort( expected.begin(), expected.end(), [ &keys ]( std::uint32_t const l, std::uint32_t const r ) { return reference_order<Order>( keys[ l ], keys[ r ] ); } );

        std::vector<std::uint32_t> perm( n, ~0U );
        argsort_keys<Algo>( keys, perm, Order{} );
        EXPECT_EQ( perm, expected ) << "shape " << static_cast<int>( s ) << ", n " << n;

        std::vector<std::uint32_t> perm_of( n, ~0U );
        argsort_by_key<Algo>( static_cast<std::uint32_t>( n ), [ &keys ]( std::uint32_t const i ) { return keys[ i ]; }, perm_of, Order{} );
        EXPECT_EQ( perm_of, expected ) << "key accessor, shape " << static_cast<int>( s ) << ", n " << n;

        std::vector<std::uint32_t> perm_of_ref( n, ~0U );
        argsort_by_key<Algo>( static_cast<std::uint32_t>( n ), [ &keys ]( std::uint32_t const i ) -> T const & { return keys[ i ]; }, perm_of_ref, Order{} );
        EXPECT_EQ( perm_of_ref, expected ) << "key accessor returning a reference, shape " << static_cast<int>( s ) << ", n " << n;

        // a 64-bit index leaves no room to pack a key beside it, so every key
        // then takes the pair worker, which is pdq's
        if constexpr ( Algo == key_sort_algo::pdq )
        {
            std::vector<std::uint64_t> const expected_64( expected.begin(), expected.end() );
            std::vector<std::uint64_t>       perm_64    ( n, ~0ULL );
            argsort_keys<Algo, std::uint64_t>( keys, perm_64, Order{} );
            EXPECT_EQ( perm_64, expected_64 ) << "64-bit index, shape " << static_cast<int>( s ) << ", n " << n;
        }
    }
}

// std::stable_sort of the indices by key is the reference, so equal keys -
// frequent in the few-distinct shape - must come out in index order.
TYPED_TEST( sort_keys_typed, pdq_argsort_agrees_with_std_stable_sort_of_indices )
{
    if constexpr ( sort_key<TypeParam> )
        check_argsort<key_sort_algo::pdq, TypeParam>();
}

#if PSI_VM_HAS_INTEGER_SORT
TYPED_TEST( sort_keys_typed, radix_argsort_agrees_with_std_stable_sort_of_indices )
{
    if constexpr ( sort_key<TypeParam> && sizeof( TypeParam ) <= sizeof( std::uint32_t ) )
        check_argsort<key_sort_algo::radix, TypeParam>();
}
#endif

// Descending, equal keys still come out in ascending index order, as
// std::stable_sort with std::greater leaves them.
TYPED_TEST( sort_keys_typed, pdq_argsort_descending_agrees_with_std_stable_sort_of_indices_reversed )
{
    if constexpr ( sort_key<TypeParam> )
        check_argsort<key_sort_algo::pdq, TypeParam, std::greater<>>();
}

#if PSI_VM_HAS_INTEGER_SORT
TYPED_TEST( sort_keys_typed, radix_argsort_descending_agrees_with_std_stable_sort_of_indices_reversed )
{
    if constexpr ( sort_key<TypeParam> && sizeof( TypeParam ) <= sizeof( std::uint32_t ) )
        check_argsort<key_sort_algo::radix, TypeParam, std::greater<>>();
}
#endif

// The index breaks ties ascending in a descending argsort on every path: the
// packed one (4-byte keys, either algorithm) and the pair one (8-byte keys,
// and 4-byte keys with a 64-bit index).
TEST( argsort_keys, descending_keeps_equal_keys_in_index_order )
{
    std::uint32_t const expected   [ 6 ]{ 0, 2, 5, 1, 4, 3 };
    std::uint64_t const expected_64[ 6 ]{ 0, 2, 5, 1, 4, 3 };
    std::uint32_t const keys_32    [ 6 ]{ 5, 3, 5, 1, 3, 5 };
    std::uint64_t const keys_64    [ 6 ]{ 5, 3, 5, 1, 3, 5 };

    std::uint32_t perm[ 6 ];
    argsort_keys( keys_32, perm, std::greater{} );
    EXPECT_TRUE( std::ranges::equal( perm, expected ) ) << "packed, pdq";
#if PSI_VM_HAS_INTEGER_SORT
    std::ranges::fill( perm, ~0U );
    argsort_keys<key_sort_algo::radix>( keys_32, perm, std::greater{} );
    EXPECT_TRUE( std::ranges::equal( perm, expected ) ) << "packed, radix";
#endif
    std::ranges::fill( perm, ~0U );
    argsort_keys( keys_64, perm, std::ranges::greater{} );
    EXPECT_TRUE( std::ranges::equal( perm, expected ) ) << "pair, 8-byte key";

    std::uint64_t perm_64[ 6 ];
    argsort_keys<key_sort_algo::pdq, std::uint64_t>( keys_32, perm_64, std::greater{} );
    EXPECT_TRUE( std::ranges::equal( perm_64, expected_64 ) ) << "pair, 64-bit index";
}

namespace
{
    // The floats that random bits hardly ever spell - both zeros, both
    // infinities, the extremes of either sign, denormals, and NaNs of either
    // sign with more than one payload each - several times over, shuffled.
    template <typename Key>
    std::vector<Key> float_specials()
    {
        using U = decltype( Key{}.bits );
        auto const one{ std::bit_cast<U>( decltype( Key{}.value() ){ 1 } ) };
        // zero, the smallest denormal, the smallest normal, one, the largest
        // finite value, infinity, and three NaNs: two quiet ones and a
        // signalling one
        U const magnitudes[]{ 0, 1, Key::mantissa + 1, one, Key::infinity - 1, Key::infinity, Key::quiet | 1, Key::quiet | 2, Key::infinity | 1 };
        std::vector<Key> keys;
        for ( int copy{ 0 }; copy < 3; ++copy )
            for ( auto const magnitude : magnitudes )
                for ( U const sign : { U{ 0 }, Key::sign } )
                    keys.push_back( Key::of( static_cast<U>( magnitude | sign ) ) );
        std::shuffle( keys.begin(), keys.end(), std::mt19937_64{ 42 } );
        return keys;
    }

    template <key_sort_algo Algo, typename Key, typename Order>
    void check_float_specials( Order const order )
    {
        auto const input{ float_specials<Key>() };
        auto const by_value{ []( Key const l, Key const r ) noexcept { return std::is_lt( std::strong_order( l.value(), r.value() ) ); } };
        auto const before  { [ by_value ]( Key const l, Key const r ) noexcept { return key_sort_detail::is_descending<Order> ? by_value( r, l ) : by_value( l, r ); } };

        auto expected{ input };
        std::stable_sort( expected.begin(), expected.end(), before );
        auto actual{ input };
        sort_keys<Algo>( std::span{ actual }, order );
        EXPECT_TRUE( same_bytes( actual, expected ) ) << "sort_keys";

        auto unique_expected{ expected };
        unique_expected.erase( std::unique( unique_expected.begin(), unique_expected.end() ), unique_expected.end() );
        auto unique_actual{ input };
        sort_unique_keys<Algo>( unique_actual, order );
        EXPECT_TRUE( same_bytes( unique_actual, unique_expected ) ) << "sort_unique_keys";

        std::vector<std::uint32_t> perm_expected( input.size() );
        std::iota( perm_expected.begin(), perm_expected.end(), 0U );
        std::stable_sort( perm_expected.begin(), perm_expected.end(), [ & ]( std::uint32_t const l, std::uint32_t const r ) { return before( input[ l ], input[ r ] ); } );
        std::vector<std::uint32_t> perm( input.size(), ~0U );
        if constexpr ( Algo == key_sort_algo::pdq || sizeof( Key ) == sizeof( std::uint32_t ) )
        {
            argsort_keys<Algo>( input, perm, order );
            EXPECT_EQ( perm, perm_expected ) << "argsort_keys";
        }
    }
} // anonymous namespace

// Ascending the floats come out as std::strong_order sorts them, and
// descending in exactly the reverse: +NaNs, +inf, ..., +0, -0, ..., -inf, -NaNs.
TEST( sort_keys, floats_order_as_std_strong_order_both_ways )
{
    check_float_specials<key_sort_algo::pdq, biased_f32>( std::less   <>{} );
    check_float_specials<key_sort_algo::pdq, biased_f32>( std::greater<>{} );
    check_float_specials<key_sort_algo::pdq, biased_f64>( std::less   <>{} );
    check_float_specials<key_sort_algo::pdq, biased_f64>( std::greater<>{} );
#if PSI_VM_HAS_INTEGER_SORT
    check_float_specials<key_sort_algo::radix, biased_f32>( std::ranges::less   {} );
    check_float_specials<key_sort_algo::radix, biased_f32>( std::ranges::greater{} );
    check_float_specials<key_sort_algo::radix, biased_f64>( std::ranges::less   {} );
    check_float_specials<key_sort_algo::radix, biased_f64>( std::ranges::greater{} );
#endif
}

// A spot check of the descending order itself, independent of the reference
// comparator: +0 and -0 in reverse strong order, NaNs at the ends.
TEST( sort_keys, descending_float_order_by_hand )
{
    std::uint32_t constexpr nan{ 0x7FC0'0000 }, one{ 0x3F80'0000 }, negative_zero{ 0x8000'0000 }, negative_infinity{ 0xFF80'0000 };
    std::vector keys{ biased_f32::of( negative_zero ), biased_f32::of( one ), biased_f32::of( nan | negative_zero ), biased_f32::of( 0 ), biased_f32::of( negative_infinity ), biased_f32::of( nan ) };
    sort_keys( std::span{ keys }, std::greater{} );
    std::uint32_t const expected[]{ nan, one, 0, negative_zero, negative_infinity, nan | negative_zero };
    ASSERT_EQ( keys.size(), std::size( expected ) );
    for ( std::size_t i{ 0 }; i < keys.size(); ++i )
        EXPECT_EQ( keys[ i ].raw(), expected[ i ] ) << "position " << i;
}

namespace
{
    // A contiguous range that reports more keys than a 32-bit index addresses
    // (while holding one): argsort_keys has to refuse it before reading any.
    struct oversized_keys
    {
        std::uint32_t const * keys;

        std::uint32_t const * begin() const noexcept { return keys; }
        std::uint32_t const * end  () const noexcept { return keys + 1; }
        static std::uint64_t  size ()       noexcept { return std::uint64_t{ std::numeric_limits<std::uint32_t>::max() } + 1; }
    };
    static_assert( std::ranges::contiguous_range<oversized_keys> && std::same_as<std::ranges::range_size_t<oversized_keys const>, std::uint64_t> );
} // anonymous namespace

TEST( argsort_keys, refuses_more_keys_than_an_index_addresses )
{
    std::uint32_t const key { 7 };
    std::uint32_t       perm{ ~0U };
    EXPECT_THROW( argsort_keys( oversized_keys{ &key }, std::span{ &perm, 1 } ), std::length_error );
    EXPECT_EQ( perm, ~0U );
}

//==============================================================================
// sort(): which partitioning it picks without being told, and that each one
// sorts
//==============================================================================

namespace
{
    // A class key that states its comparison reads only itself.
    struct direct_key
    {
        std::uint32_t value;

        static constexpr bool orders_directly{ true };

        friend constexpr auto operator<=>( direct_key, direct_key ) noexcept = default;
    };

    // Two members compared in turn: a comparison that reads only the key, and
    // branches.
    struct two_field_key
    {
        std::uint32_t major;
        std::uint32_t minor;

        static constexpr bool orders_directly{ true };

        friend constexpr auto operator<=>( two_field_key const &, two_field_key const & ) noexcept = default;
    };

    // A comparison that reads only the key, of a key that is costly to move.
    struct wide_key
    {
        std::uint64_t words[ 8 ];

        static constexpr bool orders_directly{ true };

        friend constexpr auto operator<=>( wide_key const &, wide_key const & ) noexcept = default;
    };
    static_assert( sizeof( wide_key ) == 64 );

    // A key without the statement: std::less<> on it calls an operator< that
    // could do anything.
    struct plain_key
    {
        std::uint32_t value;

        friend constexpr auto operator<=>( plain_key, plain_key ) noexcept = default;
    };

    enum struct colour : std::uint8_t  { red, green, blue };
    enum struct ticket : std::uint32_t {};

    auto constexpr int_lambda{ []( int const l, int const r ) noexcept { return l < r; } };

    // the standard comparators over scalar keys
    static_assert( key_sort_detail::branchless_by_default<std::less   <>       , int          > );
    static_assert( key_sort_detail::branchless_by_default<std::less   <int>    , int          > );
    static_assert( key_sort_detail::branchless_by_default<std::ranges::less    , int          > );
    static_assert( key_sort_detail::branchless_by_default<std::ranges::greater , double       > );
    static_assert( key_sort_detail::branchless_by_default<std::greater<>       , colour       > );
    static_assert( key_sort_detail::branchless_by_default<std::less   <>       , int const *  > );
    static_assert( key_sort_detail::branchless_by_default<erasure_opt_in<std::ranges::less>, std::uint64_t> );

    // never a class key, not even one whose comparison reads only itself:
    // orders_directly says where a comparison reads, not that it is free of
    // branches or that the key is cheap to move
    static_assert( is_direct_comparator<std::less<>, direct_key   > && !key_sort_detail::branchless_by_default<std::less<>, direct_key   > );
    static_assert( is_direct_comparator<std::less<>, two_field_key> && !key_sort_detail::branchless_by_default<std::less<>, two_field_key> );
    static_assert( is_direct_comparator<std::less<>, wide_key     > && !key_sort_detail::branchless_by_default<std::less<>, wide_key     > );
    static_assert( !key_sort_detail::branchless_by_default<std::less<direct_key>, direct_key> );
    static_assert( !key_sort_detail::branchless_by_default<std::less   <>        , plain_key                 > );
    static_assert( !key_sort_detail::branchless_by_default<std::less<plain_key>  , plain_key                 > );
    static_assert( !key_sort_detail::branchless_by_default<std::ranges::less     , plain_key                 > );
    static_assert( !key_sort_detail::branchless_by_default<std::greater<>        , std::pair<int, int>       > );
    // nor a comparator that is not a standard one
    static_assert( !key_sort_detail::branchless_by_default<decltype( int_lambda ), int                       > );

    // what a comparator states to the containers
    struct stated_branchless : std::less<> { static constexpr bool is_branchless{ true  }; };
    struct stated_branching  : std::less<> { static constexpr bool is_branchless{ false }; };
    static_assert( Komparator<std::less<>      >::partitioning == sort_partitioning::automatic  );
    static_assert( Komparator<stated_branchless>::partitioning == sort_partitioning::branchless );
    static_assert( Komparator<stated_branching >::partitioning == sort_partitioning::branching  );
    static_assert( Komparator<erasure_opt_in<stated_branching>>::partitioning == sort_partitioning::branching );

    // Keys of n / 2 + 1 values, so that equal keys are frequent.
    template <typename Key>
    std::vector<Key> make_sort_input( std::size_t const n, auto const make_key_of )
    {
        std::mt19937 rng{ 7 };
        std::vector<Key> keys( n );
        for ( auto & key : keys )
            key = make_key_of( static_cast<std::uint32_t>( rng() % ( n / 2 + 1 ) ) );
        return keys;
    }

    auto constexpr as_is           { []( std::uint32_t const v ) noexcept { return v; } };
    auto constexpr as_ticket       { []( std::uint32_t const v ) noexcept { return ticket{ v }; } };
    auto constexpr as_two_field_key{ []( std::uint32_t const v ) noexcept { return two_field_key{ v >> 2, v & 3 }; } };
    auto constexpr as_wide_key     { []( std::uint32_t const v ) noexcept { wide_key key; std::ranges::fill( key.words, v ); return key; } };
} // anonymous namespace

template <typename Key, sort_partitioning Partitioning = sort_partitioning::automatic, typename Comparator, typename MakeKey>
void check_sort( Comparator const comp, MakeKey const make_key_of )
{
    for ( auto const n : sizes )
    {
        auto actual  { make_sort_input<Key>( n, make_key_of ) };
        auto expected{ actual };
        std::stable_sort( expected.begin(), expected.end(), comp );
        vm::sort<comparator_erasure::never, Partitioning>( actual.begin(), actual.end(), comp );
        EXPECT_TRUE( same_bytes( actual, expected ) ) << "n " << n;
    }
}

template <typename Key, typename Comparator, typename MakeKey>
void check_every_partitioning( Comparator const comp, MakeKey const make_key_of )
{
    check_sort<Key, sort_partitioning::automatic >( comp, make_key_of );
    check_sort<Key, sort_partitioning::branchless>( comp, make_key_of );
    check_sort<Key, sort_partitioning::branching >( comp, make_key_of );
}

TEST( sort, agrees_with_std_stable_sort_whichever_partitioning_it_picks_or_is_told )
{
    std::vector<int> const pointees( sizes[ std::size( sizes ) - 1 ] / 2 + 1 );
    auto const as_pointer{ [ &pointees ]( std::uint32_t const v ) noexcept { return &pointees[ v ]; } };

    check_every_partitioning<direct_key   >( std::less<>{}, []( std::uint32_t const v ) noexcept { return direct_key{ v }; } );
    check_every_partitioning<plain_key    >( std::less<>{}, []( std::uint32_t const v ) noexcept { return plain_key { v }; } );
    check_every_partitioning<two_field_key>( std::less<>{}, as_two_field_key );
    check_every_partitioning<wide_key     >( std::less<>{}, as_wide_key );
    check_every_partitioning<std::uint32_t>( std::less<std::uint32_t>{}, as_is );
    check_every_partitioning<std::uint32_t>( std::ranges::less   {}, as_is );
    check_every_partitioning<std::uint32_t>( std::ranges::greater{}, as_is );
    check_every_partitioning<std::uint32_t>( []( std::uint32_t const l, std::uint32_t const r ) noexcept { return l < r; }, as_is );
    check_every_partitioning<ticket       >( std::less   <>{}, as_ticket );
    check_every_partitioning<ticket       >( std::greater<>{}, as_ticket );
    check_every_partitioning<int const *  >( std::less<>{}, as_pointer );
    check_every_partitioning<int const *  >( std::ranges::greater{}, as_pointer );
}

// The containers' sorts go through Komparator::sort with the partitioning the
// comparator states, or sort()'s own choice where it states none.
template <typename Comparator, typename Key, typename MakeKey>
void check_komparator_sort( MakeKey const make_key_of )
{
    for ( auto const n : sizes )
    {
        auto actual  { make_sort_input<Key>( n, make_key_of ) };
        auto expected{ actual };
        std::stable_sort( expected.begin(), expected.end(), Comparator{} );
        Komparator<Comparator>{}.sort( actual.begin(), actual.end() );
        EXPECT_TRUE( same_bytes( actual, expected ) ) << "n " << n;
    }
}

TEST( Komparator, sort_agrees_with_std_stable_sort_by_default_and_as_stated )
{
    check_komparator_sort<std::less<>      , std::uint32_t>( as_is );
    check_komparator_sort<std::less<>      , ticket       >( as_ticket );
    check_komparator_sort<std::less<>      , two_field_key>( as_two_field_key );
    check_komparator_sort<stated_branchless, ticket       >( as_ticket );
    check_komparator_sort<stated_branchless, two_field_key>( as_two_field_key );
    check_komparator_sort<stated_branching , std::uint32_t>( as_is );
    check_komparator_sort<stated_branching , ticket       >( as_ticket );
}

// The default path through a container: a bulk insert of enumeration keys,
// which the default comparator sorts with the branchless partitioning.
TEST( flat_set, bulk_insert_of_enumeration_keys_agrees_with_std_sort_and_unique )
{
    for ( auto const n : sizes )
    {
        auto const input{ make_sort_input<ticket>( n, as_ticket ) };
        auto expected{ input };
        std::sort( expected.begin(), expected.end() );
        expected.erase( std::unique( expected.begin(), expected.end() ), expected.end() );

        flat_set<ticket> set;
        set.insert( input.begin(), input.end() );
        EXPECT_TRUE( std::ranges::equal( set, expected ) ) << "n " << n;
    }
}

namespace
{
    // Up to three words of a four-letter alphabet, so that equal sequences,
    // and sequences that are prefixes of one another, are frequent.
    template <typename Sequences>
    Sequences make_sequences( std::size_t const n )
    {
        std::mt19937 rng{ 11 };
        Sequences sequences( n );
        for ( auto & sequence : sequences )
        {
            auto const length{ rng() % 4 };
            for ( std::uint32_t i{ 0 }; i < length; ++i )
                sequence.push_back( static_cast<std::uint32_t>( rng() % 4 ) );
        }
        return sequences;
    }

    template <typename Sequences>
    std::vector<std::vector<std::uint32_t>> as_vectors( Sequences const & sequences )
    {
        std::vector<std::vector<std::uint32_t>> vectors;
        for ( auto const & sequence : sequences )
            vectors.emplace_back( sequence.begin(), sequence.end() );
        return vectors;
    }

    std::vector<std::vector<std::uint32_t>> std_sort_unique( std::vector<std::vector<std::uint32_t>> sequences )
    {
        std::sort( sequences.begin(), sequences.end() );
        sequences.erase( std::unique( sequences.begin(), sequences.end() ), sequences.end() );
        return sequences;
    }
} // anonymous namespace

// The sort swaps the elements, which for a small_vector with inline storage
// moves them; sort_unique's noexcept holds only while those moves cannot throw.
static_assert( std::is_nothrow_move_constructible_v<small_vector<std::uint32_t, 4>> && std::is_nothrow_move_assignable_v<small_vector<std::uint32_t, 4>> );

TEST( sort_unique, a_range_of_ranges_agrees_with_std_sort_and_unique )
{
    for ( auto const n : sizes )
    {
        // heap vectors, through the iterator overload
        auto actual{ make_sequences<std::vector<std::vector<std::uint32_t>>>( n ) };
        auto const expected{ std_sort_unique( actual ) };
        auto const kept{ sort_unique( actual.begin(), actual.end(), std::ranges::lexicographical_compare ) };
        actual.erase( kept, actual.end() );
        EXPECT_EQ( actual, expected ) << "n " << n;

        // small vectors with inline storage, through the container overload
        auto inline_actual{ make_sequences<small_vector<small_vector<std::uint32_t, 4>, 8>>( n ) };
        auto const inline_expected{ std_sort_unique( as_vectors( inline_actual ) ) };
        sort_unique( inline_actual, std::ranges::lexicographical_compare );
        EXPECT_EQ( as_vectors( inline_actual ), inline_expected ) << "n " << n;
    }
}

TEST( sort_unique, a_descending_order_agrees_with_std_sort_and_unique )
{
    for ( auto const n : sizes )
    {
        auto actual  { make_sort_input<std::uint32_t>( n, as_is ) };
        auto expected{ actual };
        std::sort( expected.begin(), expected.end(), std::greater<>{} );
        expected.erase( std::unique( expected.begin(), expected.end() ), expected.end() );
        sort_unique( actual, std::ranges::greater{} );
        EXPECT_EQ( actual, expected ) << "n " << n;
    }
}

// Elements that are equivalent without being equal: one of each run is kept,
// which one is unspecified.
TEST( sort_unique, keeps_one_element_of_each_run_of_equivalent_ones )
{
    using element = std::pair<std::uint32_t, std::uint32_t>;
    auto const by_first{ []( element const & left, element const & right ) noexcept { return left.first < right.first; } };
    for ( auto const n : sizes )
    {
        auto const input{ make_sort_input<element>( n, []( std::uint32_t const v ) noexcept { return element{ v / 4, v }; } ) };
        auto actual{ input };
        sort_unique( actual, by_first );

        std::vector<std::uint32_t> expected_firsts;
        for ( auto const & e : input )
            expected_firsts.push_back( e.first );
        std::sort( expected_firsts.begin(), expected_firsts.end() );
        expected_firsts.erase( std::unique( expected_firsts.begin(), expected_firsts.end() ), expected_firsts.end() );

        std::vector<std::uint32_t> actual_firsts;
        for ( auto const & e : actual )
            actual_firsts.push_back( e.first );
        EXPECT_EQ( actual_firsts, expected_firsts ) << "n " << n;

        auto sorted_input{ input };
        std::sort( sorted_input.begin(), sorted_input.end() );
        EXPECT_TRUE( std::ranges::all_of( actual, [ & ]( element const & e ) { return std::binary_search( sorted_input.begin(), sorted_input.end(), e ); } ) ) << "n " << n;
    }
}

namespace
{
    // Distinct items with gaps (every third index, shuffled), keys drawn from
    // key_bits bits - few distinct values at small widths, so equal keys and
    // their tie order are exercised too.
    template <std::unsigned_integral Item>
    struct by_key_case
    {
        std::vector<Item>          items;
        std::vector<std::uint64_t> key_of_item;
    };

    template <std::unsigned_integral Item>
    by_key_case<Item> make_by_key_case( std::size_t const n, unsigned const key_bits, std::uint64_t const seed )
    {
        by_key_case<Item> c;
        std::mt19937_64 rng{ seed };
        c.items.resize( n );
        for ( std::size_t i{ 0 }; i < n; ++i )
            c.items[ i ] = static_cast<Item>( i * 3 );
        std::shuffle( c.items.begin(), c.items.end(), rng );
        c.key_of_item.resize( n * 3 );
        auto const mask{ key_bits >= 64 ? ~std::uint64_t{ 0 } : ( std::uint64_t{ 1 } << key_bits ) - 1 };
        for ( auto & k : c.key_of_item )
            k = rng() & mask;
        return c;
    }

    template <std::unsigned_integral Item>
    std::vector<Item> by_key_reference( by_key_case<Item> const & c )
    {
        auto expected{ c.items };
        std::ranges::sort( expected, [ &c ]( Item const l, Item const r ) { auto const kl{ c.key_of_item[ l ] }, kr{ c.key_of_item[ r ] }; return ( kl < kr ) || ( ( kl == kr ) && ( l < r ) ); } );
        return expected;
    }

    template <key_sort_algo Algo>
    void check_sort_by_key()
    {
        std::uint64_t seed{ 3'000 };
        for ( auto const key_bits : { 0U, 1U, 8U, 20U, 33U, 48U, 64U } )
        for ( auto const n : { 0UZ, 1UZ, 2UZ, 7UZ, 300UZ, 5'000UZ } )
        for ( auto const bucket_limit : { 2UZ, 16UZ, 1'000UZ, sort_by_key_bucket_limit } )
        {
            auto c{ make_by_key_case<std::uint32_t>( n, key_bits, ++seed ) };
            auto const expected{ by_key_reference( c ) };
            sort_by_key<Algo>( c.items.begin(), c.items.end(), [ &c ]( std::uint32_t const item ) { return c.key_of_item[ item ]; }, key_bits, bucket_limit );
            EXPECT_EQ( c.items, expected ) << "key bits " << key_bits << ", n " << n << ", bucket limit " << bucket_limit;
        }
    }
} // anonymous namespace

TEST( sort_by_key, pdq_agrees_with_std_sort_by_key_then_item )
{
    check_sort_by_key<key_sort_algo::pdq>();
}

#if PSI_VM_HAS_INTEGER_SORT
TEST( sort_by_key, radix_agrees_with_std_sort_by_key_then_item )
{
    check_sort_by_key<key_sort_algo::radix>();
}
#endif

// Items in order already are recognised by one pass that reads each key
// once, and left as they were.
TEST( sort_by_key, items_in_order_cost_one_key_each_and_stay )
{
    auto c{ make_by_key_case<std::uint32_t>( 10'000, 40, 42 ) };
    c.items = by_key_reference( c );
    auto const expected{ c.items };
    std::size_t keys_read{ 0 };
    sort_by_key( c.items.begin(), c.items.end(), [ & ]( std::uint32_t const item ) { ++keys_read; return c.key_of_item[ item ]; }, 40 );
    EXPECT_EQ( c.items, expected );
    EXPECT_EQ( keys_read, c.items.size() );
}

// Any random-access range, not only a contiguous one; and a caller's own
// worker never gets more than a bucket of keys at a time.
TEST( sort_by_key, sorts_a_deque_through_a_caller_worker_a_bucket_at_a_time )
{
    auto c{ make_by_key_case<std::uint32_t>( 20'000, 30, 7 ) };
    auto const expected{ by_key_reference( c ) };
    std::deque<std::uint32_t> items( c.items.begin(), c.items.end() );
    std::size_t largest{ 0 }, calls{ 0 };
    sort_by_key
    (
        items.begin(), items.end(),
        [ &c ]( std::uint32_t const item ) { return c.key_of_item[ item ]; }, 30,
        [ & ]( std::span<std::uint64_t> const keys ) { ++calls; largest = std::max( largest, keys.size() ); std::ranges::sort( keys ); },
        1'024
    );
    EXPECT_TRUE( std::ranges::equal( items, expected ) );
    EXPECT_GT( calls, 1U );
    EXPECT_LE( largest, 1'024U );
}

// 64-bit keys beside 64-bit items never fit together: the ranges are
// partitioned down to equal keys, whose items are then ordered in place.
TEST( sort_by_key, wide_keys_beside_wide_items_still_sort )
{
    std::mt19937_64 rng{ 11 };
    std::vector<std::uint64_t> items( 3'000 );
    for ( auto & item : items )
        item = rng() | ( std::uint64_t{ 1 } << 63 );
    auto const key_of{ []( std::uint64_t const item ) { return ( item * 0x9E37'79B9'7F4A'7C15ULL ) & 0xFFFF'0000'0000'00FFULL; } };
    auto expected{ items };
    std::ranges::sort( expected, [ & ]( std::uint64_t const l, std::uint64_t const r ) { return std::pair{ key_of( l ), l } < std::pair{ key_of( r ), r }; } );
    sort_by_key( items.begin(), items.end(), key_of, 64, 64 );
    EXPECT_EQ( items, expected );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
