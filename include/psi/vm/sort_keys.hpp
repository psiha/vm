////////////////////////////////////////////////////////////////////////////////
/// psi::vm::sort_keys -- sorting of keys that order as unsigned integers,
/// through one out-of-line worker per (algorithm, key width).
///
/// Every call of pdqsort or spreadsort stamps the whole algorithm for its
/// iterator and comparator types. The result of sorting keys that order as
/// the unsigned integer their bytes spell does not depend on the type that
/// spells them, so here every such key type of a width is sorted by one worker
/// over that width's unsigned integer: a program gets at most one worker per
/// (algorithm, width) pair, whatever key types it sorts. argsort_keys and
/// argsort_by_key reuse the 64-bit workers for 4-byte keys with 32-bit
/// indices, and add one pdqsort worker per (key width, index width) pair for
/// the keys that leave no room to pack their index beside them.
///
/// The radix algorithm lives in psi/vm/sort_keys_radix.hpp, so that only a
/// program that asks for it includes spreadsort.
///
/// Copyright (c) Domagoj Saric.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
/// (See accompanying file LICENSE_1_0.txt or copy at
/// http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once

#include "sort.hpp" // PSI_VM_PDQSORT_BRANCHLESS
#include "containers/small_vector.hpp"

#include <psi/build/attributes.hpp>
#include <psi/build/disable_warnings.hpp>

#include <boost/assert.hpp>

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

PSI_WARNING_DISABLE_PUSH()
PSI_WARNING_MSVC_DISABLE( 5030 ) // unrecognized attribute

/// The algorithm that sorts the keys.
///  * pdq   -- pdqsort_branchless: in place, allocates nothing, and adapts to
///             runs that are already sorted; its worst case is bounded.
///  * radix -- spreadsort::integer_sort, from psi/vm/sort_keys_radix.hpp:
///             allocates its bins on every call (see key_sort_nothrow below);
///             below its size threshold it runs pdqsort itself. Only ever used
///             when asked for by name.
enum struct key_sort_algo : std::uint8_t { pdq, radix };

// The internals have a namespace of their own rather than detail: each
// platform's implementation namespace (psi::vm::win32, psi::vm::posix, both
// inline) has a detail too, so wherever one of those has been declared - a
// translation unit that includes the shared memory headers first - detail
// names no single namespace from within psi::vm, however it is qualified.
namespace key_sort_detail
{
    // The unsigned integer of a key's size, which the worker sorts in the
    // key's place.
    template <typename T>
    using key_uint_t = std::conditional_t<sizeof( T ) == sizeof( std::uint32_t ), std::uint32_t, std::uint64_t>;
} // namespace key_sort_detail

/// A key that sorts as the unsigned integer of its own size, 4 or 8 bytes:
///  * an unsigned integral type, or
///  * a trivially copyable type without padding bits that states
///        static constexpr bool orders_as_unsigned{ true };
///    which is the promise that its object representation, read as the
///    unsigned integer of its size (in the target's byte order), orders
///    exactly as the key does - e.g. a strong typedef around an unsigned
///    integer, or two unsigned fields laid out so that the more significant
///    one occupies the high half of that integer on the target. The key type
///    is where that layout is known, so it is the key that states it (with
///    whatever static_asserts it needs about its layout and byte order).
/// The sort creates that integer in each key's own storage, so the key must
/// be at least as aligned as the integer. A key made of narrower fields gets
/// only their alignment from them, so it has to declare the integer's -
/// alignas( std::uint64_t ) for two 32-bit fields - or it is not a sort_key.
///
/// A signed integer or a floating-point value does not order as its bits read
/// unsigned, but a key can hold it biased so that it does: a signed integer
/// with its sign bit flipped (x ^ 0x8000'0000 for 32 bits), an IEEE float with
/// all of its bits flipped when its sign bit is set and only the sign bit
/// flipped when it is not - which orders the values as std::strong_order does
/// the floats, -0.0 before +0.0 and NaNs at either end. Nothing can check the
/// promise at compile time, so a debug build checks each sort's result
/// against the key's own operator<, where it has one.
template <typename T>
concept sort_key =
    ( sizeof( T ) == sizeof( std::uint32_t ) || sizeof( T ) == sizeof( std::uint64_t ) ) &&
    alignof( T ) >= alignof( key_sort_detail::key_uint_t<T> ) &&
    !std::is_const_v<T> && !std::is_volatile_v<T> &&
    (
        std::unsigned_integral<T> ||
        (
            requires { requires( T::orders_as_unsigned ); } &&
            std::is_trivially_copyable_v<T> && std::has_unique_object_representations_v<T>
        )
    );

namespace key_sort_detail
{
    // radix allocates its bins, so a radix sort cannot fail only where an
    // allocation cannot, which is what PSI_NOEXCEPT_EXCEPT_BADALLOC states
    // for the build; the pdq path allocates nothing and is always noexcept.
    using may_fail_to_allocate = void() PSI_NOEXCEPT_EXCEPT_BADALLOC;
    bool constexpr allocation_nothrow{ noexcept( std::declval<may_fail_to_allocate &>()() ) };

    template <key_sort_algo Algo>
    bool constexpr key_sort_nothrow{ Algo == key_sort_algo::pdq || allocation_nothrow };

    // How an algorithm sorts a range of unsigned integers. pdq is defined
    // here and radix in psi/vm/sort_keys_radix.hpp; an algorithm whose
    // definition has not been included stops the build here. Each sort() is
    // inlined into the worker that calls it, which stays the one out-of-line
    // body per (algorithm, key width), and leaves noexcept to that worker.
    template <key_sort_algo Algo>
    struct algorithm
    {
        static_assert( Algo != key_sort_algo::radix, "key_sort_algo::radix needs psi/vm/sort_keys_radix.hpp, and Boost.Sort's spreadsort headers" );
    };

    template <>
    struct algorithm<key_sort_algo::pdq>
    {
        template <std::unsigned_integral U>
        [[ gnu::always_inline ]] static void sort( U * const first, U * const last ) { PSI_VM_PDQSORT_BRANCHLESS( first, last, std::less<U>{} ); }
    };

    // Ends the lifetime of the n objects at p and starts that of n objects of
    // type To in the same storage, holding the same bytes - exactly what
    // std::start_lifetime_as_array does. Where the standard library does not
    // provide it - libc++, and MSVC's library under clang-cl and under a cl
    // older than 19.51 - memmove stands in for it: it implicitly creates
    // objects in its destination, of whatever types give the program defined
    // behaviour, and returns a pointer to them ([cstring.syn]). So a key's
    // storage is handed to the worker as integers, and handed back as keys,
    // without an access through a type the object does not have.
    //
    // What the hand-over costs is the implementation's. start_lifetime_as_array
    // emits no instruction in libstdc++ 16 or in MSVC's library under cl 19.51.
    // A memmove onto itself changes no byte; GCC 16 deletes the call even
    // without optimisation and clang 22 whenever it optimises, but cl 19.51
    // keeps it, so where cl takes the memmove path, and in an unoptimised
    // clang build, each sort copies the keys in place on the way to the worker
    // and again on the way back.
    template <typename To>
    [[ nodiscard ]] To * reuse_storage_as( void * const p, std::size_t const n ) noexcept
    {
#ifdef __cpp_lib_start_lifetime_as
        return std::start_lifetime_as_array<To>( p, n );
#else
        return static_cast<To *>( std::memmove( p, p, n * sizeof( To ) ) );
#endif
    }

    // The one out-of-line body per (algorithm, key width): sorts, and with
    // unique also moves the first of each run of equal keys to the front,
    // returning how many keys it keeps. Deduplication is a runtime argument
    // rather than a template one so that it costs no second instantiation of
    // the sort.
    template <key_sort_algo Algo, std::unsigned_integral U>
    [[ gnu::noinline, gnu::sysv_abi ]]
    std::size_t sort_uints( std::span<U> const keys, bool const unique ) noexcept( key_sort_nothrow<Algo> )
    {
        auto * const first{ keys.data() };
        auto * const last { keys.data() + keys.size() };
        algorithm<Algo>::sort( first, last );
        if ( !unique )
            return keys.size();
        return static_cast<std::size_t>( std::unique( first, last ) - first );
    }

    template <key_sort_algo Algo, sort_key T>
    std::size_t sort_as_uints( std::span<T> const keys, bool const unique ) noexcept( key_sort_nothrow<Algo> )
    {
        using uint_t = key_uint_t<T>;
        if ( keys.size() < 2 )
            return keys.size();
        if constexpr ( std::is_same_v<T, uint_t> )
        {
            return sort_uints<Algo>( keys, unique );
        }
        else
        {
            auto const kept
            {
                [ keys, unique ]() noexcept( key_sort_nothrow<Algo> )
                {
                    // From here on the storage holds integers, and it has to
                    // hold the caller's keys again however the sort ends -
                    // radix can also end it by throwing, where allocation can
                    // fail - so a destructor hands it back. Where the sort
                    // cannot throw, that is the same code as a hand-back after
                    // the call.
                    struct keys_restorer
                    {
                        uint_t      * const uints;
                        std::size_t   const size;

                        ~keys_restorer() noexcept { std::ignore = reuse_storage_as<T>( uints, size ); }
                    } const restorer{ reuse_storage_as<uint_t>( keys.data(), keys.size() ), keys.size() };
                    return sort_uints<Algo>( std::span<uint_t>{ restorer.uints, restorer.size }, unique );
                }()
            };
            // The key's promise to order as its unsigned reading (see
            // sort_key) is checked here, where it can be: a debug build
            // compares the result with the key's own ordering.
            if constexpr ( requires( T const & key ) { { key < key } -> std::convertible_to<bool>; } )
            {
                BOOST_ASSERT_MSG( std::is_sorted( keys.data(), keys.data() + kept ), "a key that states orders_as_unsigned does not order as its unsigned reading" );
            }
            return kept;
        }
    }

    // A key that leaves no room to pack its index beside it, and that index.
    template <std::unsigned_integral Key, std::unsigned_integral Index>
    struct key_index
    {
        Key   key;
        Index index;
    };

    // The argsort worker for such keys: sorts ( key, index ) pairs by key and
    // then by index, and writes out the indices.
    template <std::unsigned_integral Key, std::unsigned_integral Index>
    [[ gnu::noinline, gnu::sysv_abi ]]
    void argsort_pairs( std::span<key_index<Key, Index>> const pairs, std::span<Index> const perm ) noexcept
    {
        BOOST_ASSERT( perm.size() == pairs.size() );
        PSI_VM_PDQSORT_BRANCHLESS
        (
            pairs.data(), pairs.data() + pairs.size(),
            []( key_index<Key, Index> const & left, key_index<Key, Index> const & right ) noexcept -> bool
            {
                return ( left.key < right.key ) | ( ( left.key == right.key ) & ( left.index < right.index ) );
            }
        );
        for ( std::size_t i{ 0 }; i < pairs.size(); ++i )
            perm[ i ] = pairs[ i ].index;
    }

    // The size type of the scratch storage: wide enough for any count of
    // Index, and never narrower than 32 bits, the inline capacity's.
    template <typename Index>
    using scratch_size_t = std::common_type_t<Index, std::uint32_t>;

    // Whether a range can hold more keys than an index of type Index counts.
    template <typename Keys, typename Index>
    bool constexpr may_exceed_index{ std::numeric_limits<std::ranges::range_size_t<Keys>>::max() > std::numeric_limits<Index>::max() };

    // Whether such a count is a condition to report rather than a programming
    // error to assert on - the rule heap_storage::length_error_is_reportable
    // applies to its byte counter: below 64 bits an ordinary count of keys
    // reaches the limit, at 64 bits none plausibly does.
    template <typename Keys, typename Index>
    bool constexpr index_overflow_is_reportable{ may_exceed_index<Keys, Index> && sizeof( Index ) < sizeof( std::uint64_t ) };

    // Reports such a count as detail::length_error reports a size a container
    // cannot hold: by std::length_error where it is reportable, by an
    // assertion where it is not. It is detail::length_error itself once
    // psi::vm::detail names a single namespace again (see the note on
    // key_sort_detail above).
    template <bool Reportable>
    [[ noreturn, gnu::noinline ]] PSI_COLD void length_error() noexcept( !Reportable )
    {
        if constexpr ( Reportable )
        {
            throw std::length_error{ "psi::vm::argsort_keys: more keys than the index type counts" };
        }
        else
        {
            BOOST_ASSERT_MSG( false, "more keys than the index type counts" );
            std::unreachable();
        }
    }
} // namespace key_sort_detail

/// Sorts the keys ascending (in the order of their unsigned reading), by
/// pdqsort unless radix is asked for. Not stable - which is not observable:
/// keys that compare equal have the same object representation.
/// noexcept for pdq; for radix only where allocation cannot fail
/// (PSI_NOEXCEPT_EXCEPT_BADALLOC).
template <key_sort_algo Algo = key_sort_algo::pdq, sort_key T>
void sort_keys( std::span<T> const keys ) noexcept( key_sort_detail::key_sort_nothrow<Algo> )
{
    std::ignore = key_sort_detail::sort_as_uints<Algo>( keys, false );
}

/// Sorts the keys as sort_keys does and moves the first of each run of equal
/// keys to the front, in order, as std::unique does; returns how many keys
/// that is. The keys past that count are left with unspecified values.
template <key_sort_algo Algo = key_sort_algo::pdq, sort_key T>
[[ nodiscard ]] std::size_t sort_unique_keys( std::span<T> const keys ) noexcept( key_sort_detail::key_sort_nothrow<Algo> )
{
    return key_sort_detail::sort_as_uints<Algo>( keys, true );
}

/// Sorts and deduplicates a contiguous container of keys and truncates it to
/// the unique keys. noexcept on the same terms as the span overload, which
/// takes it that resize() to a smaller size does not throw: true of the
/// standard and psi::vm containers, whose shrinking resize neither allocates
/// nor constructs, but a property of the container rather than one checked
/// here - resize() is noexcept for none of them, as growing allocates.
template <key_sort_algo Algo = key_sort_algo::pdq, std::ranges::contiguous_range Keys>
requires( sort_key<std::ranges::range_value_t<Keys>> && requires( Keys & keys ) { keys.resize( std::ranges::size( keys ) ); } )
void sort_unique_keys( Keys & keys ) noexcept( key_sort_detail::key_sort_nothrow<Algo> )
{
    auto const kept{ sort_unique_keys<Algo>( std::span<std::ranges::range_value_t<Keys>>{ keys } ) };
    keys.resize( static_cast<std::ranges::range_size_t<Keys>>( kept ) );
}

/// Writes to perm the permutation that sorts the n keys key_of( 0 ), ...,
/// key_of( n - 1 ): perm[ k ] is the index of the k-th smallest key. The
/// index breaks ties, so equal keys keep the order of their indices - the
/// result is that of a stable sort. Index is the type of the indices (and of
/// n), 32 bits unless a wider one is asked for.
///  * a 4-byte key with an index of at most 32 bits is packed with it into one
///    64-bit integer, key << 32 | index, which the sort_keys worker sorts -
///    with Algo;
///  * any other key is sorted as a ( key, index ) pair by pdqsort, the only
///    algorithm offered for them.
/// The keys are gathered, with their indices, into scratch storage whose
/// first 4 KiB are on the stack - every call takes that much stack, whatever
/// n is - and the rest on the heap, so the call is noexcept only where
/// allocation cannot fail (and key_of does not throw).
template <key_sort_algo Algo = key_sort_algo::pdq, std::unsigned_integral Index = std::uint32_t, typename KeyOf>
requires sort_key<std::remove_cvref_t<std::invoke_result_t<KeyOf &, Index>>>
void argsort_by_key( std::type_identity_t<Index> const n, KeyOf && key_of, std::span<std::type_identity_t<Index>> const perm )
    noexcept( key_sort_detail::allocation_nothrow && std::is_nothrow_invocable_v<KeyOf &, Index> )
{
    using key_t = std::remove_cvref_t<std::invoke_result_t<KeyOf &, Index>>;
    BOOST_ASSERT( perm.size() == n );
    if constexpr ( sizeof( key_t ) == sizeof( std::uint32_t ) && sizeof( Index ) <= sizeof( std::uint32_t ) )
    {
        small_vector<std::uint64_t, 512, key_sort_detail::scratch_size_t<Index>> packed( n, no_init );
        for ( Index i{ 0 }; i < n; ++i )
            packed[ i ] = ( std::uint64_t{ std::bit_cast<std::uint32_t>( key_t{ key_of( i ) } ) } << 32 ) | i;
        std::ignore = key_sort_detail::sort_uints<Algo>( std::span<std::uint64_t>{ packed }, false );
        for ( Index i{ 0 }; i < n; ++i )
            perm[ i ] = static_cast<Index>( packed[ i ] );
    }
    else
    {
        static_assert( Algo == key_sort_algo::pdq, "keys that leave no room for their index are argsorted by pdqsort only" );
        using key_uint = key_sort_detail::key_uint_t<key_t>;
        using pair     = key_sort_detail::key_index<key_uint, Index>;
        small_vector<pair, 4096 / sizeof( pair ), key_sort_detail::scratch_size_t<Index>> pairs( n, no_init );
        for ( Index i{ 0 }; i < n; ++i )
            pairs[ i ] = { std::bit_cast<key_uint>( key_t{ key_of( i ) } ), i };
        key_sort_detail::argsort_pairs<key_uint, Index>( pairs, perm );
    }
}

/// The permutation that sorts a contiguous range of keys (see argsort_by_key).
/// A range of more keys than an Index counts is refused with
/// std::length_error, in every build - counting it in an Index would sort only
/// some of the keys and leave the rest of perm unwritten - by the rule the
/// containers apply to a size they cannot hold: reported where an ordinary
/// count of keys can reach it (Index below 64 bits), asserted on where none
/// plausibly can. A range whose size type an Index covers needs no check, so
/// with a 64-bit Index the call is noexcept wherever allocation cannot fail.
template <key_sort_algo Algo = key_sort_algo::pdq, std::unsigned_integral Index = std::uint32_t, std::ranges::contiguous_range Keys>
requires sort_key<std::ranges::range_value_t<Keys>>
void argsort_keys( Keys const & keys, std::span<std::type_identity_t<Index>> const perm )
    noexcept( key_sort_detail::allocation_nothrow && !key_sort_detail::index_overflow_is_reportable<Keys const, Index> )
{
    auto const size{ std::ranges::size( keys ) };
    if constexpr ( key_sort_detail::may_exceed_index<Keys const, Index> )
    {
        if ( size > std::numeric_limits<Index>::max() ) [[ unlikely ]]
            key_sort_detail::length_error<key_sort_detail::index_overflow_is_reportable<Keys const, Index>>();
    }
    auto const * const data{ std::ranges::data( keys ) };
    argsort_by_key<Algo, Index>
    (
        static_cast<Index>( size ),
        [ data ]( Index const i ) noexcept { return data[ i ]; },
        perm
    );
}

PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
