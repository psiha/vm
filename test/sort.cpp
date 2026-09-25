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

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <compare>
#include <concepts>
#include <cstdint>
#include <cstring>
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
    // argsort_keys gathers into scratch storage that spills to the heap, and
    // refuses a range of more keys than its index type counts, which only a
    // range with a wider size type can hold.
    static_assert( noexcept( argsort_keys( std::declval<heap_vector<std::uint32_t, std::uint32_t> const &>(), std::span<std::uint32_t>{} ) ) == key_sort_detail::allocation_nothrow );
    static_assert( noexcept( argsort_keys( std::span<std::uint32_t const>{}, std::span<std::uint32_t>{} ) ) == ( key_sort_detail::allocation_nothrow && sizeof( std::size_t ) == sizeof( std::uint32_t ) ) );
    static_assert( noexcept( argsort_keys<key_sort_algo::pdq, std::uint64_t>( std::span<std::uint32_t const>{}, std::span<std::uint64_t>{} ) ) == key_sort_detail::allocation_nothrow );
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

template <key_sort_algo Algo, typename T>
void check_argsort()
{
    std::uint64_t seed{ 2'000 };
    for ( auto const s : all_shapes )
    for ( auto const n : sizes )
    {
        auto const keys{ make_input<T>( s, n, ++seed ) };

        std::vector<std::uint32_t> expected( n );
        std::iota( expected.begin(), expected.end(), 0U );
        std::stable_sort( expected.begin(), expected.end(), [ &keys ]( std::uint32_t const l, std::uint32_t const r ) { return keys[ l ] < keys[ r ]; } );

        std::vector<std::uint32_t> perm( n, ~0U );
        argsort_keys<Algo>( keys, perm );
        EXPECT_EQ( perm, expected ) << "shape " << static_cast<int>( s ) << ", n " << n;

        std::vector<std::uint32_t> perm_of( n, ~0U );
        argsort_by_key<Algo>( static_cast<std::uint32_t>( n ), [ &keys ]( std::uint32_t const i ) { return keys[ i ]; }, perm_of );
        EXPECT_EQ( perm_of, expected ) << "key accessor, shape " << static_cast<int>( s ) << ", n " << n;

        std::vector<std::uint32_t> perm_of_ref( n, ~0U );
        argsort_by_key<Algo>( static_cast<std::uint32_t>( n ), [ &keys ]( std::uint32_t const i ) -> T const & { return keys[ i ]; }, perm_of_ref );
        EXPECT_EQ( perm_of_ref, expected ) << "key accessor returning a reference, shape " << static_cast<int>( s ) << ", n " << n;

        // a 64-bit index leaves no room to pack a key beside it, so every key
        // then takes the pair worker, which is pdq's
        if constexpr ( Algo == key_sort_algo::pdq )
        {
            std::vector<std::uint64_t> const expected_64( expected.begin(), expected.end() );
            std::vector<std::uint64_t>       perm_64    ( n, ~0ULL );
            argsort_keys<Algo, std::uint64_t>( keys, perm_64 );
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
    if constexpr ( sort_key<TypeParam> && sizeof( TypeParam ) == sizeof( std::uint32_t ) )
        check_argsort<key_sort_algo::radix, TypeParam>();
}
#endif

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

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
