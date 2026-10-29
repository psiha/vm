////////////////////////////////////////////////////////////////////////////////
/// psi::vm::sorted_view -- the keys of an attributed_span, sorted, with the
/// least work its facts allow: the span itself when it is already sorted, an
/// in place sort when it may be permuted, a sorted copy otherwise.
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

#include "attributed_span.hpp"
#include "sort.hpp"
#include "sort_keys.hpp"

#include <algorithm>
#include <bit>
#include <functional>
#include <span>
#include <iterator>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

/// The keys in the order of the span's Comparator, as a read only view that is
/// sorted (and, with Unique, has each key once). Which storage it views depends
/// on the facts of `keys`:
///  * already sorted (and unique, if asked for): `keys` itself, nothing done;
///  * sorted but with repeated keys, Unique asked for: deduplicated in place if
///    `keys` may be permuted, else copied deduplicated into `scratch`;
///  * not sorted, `keys` may be permuted: sorted in place;
///  * otherwise: copied into `scratch`, sorted there.
/// `scratch` is any container of the keys with assign( first, last ), clear(),
/// resize() and data() (e.g. a vector); it is only written where the keys are
/// copied: when `keys` may not be permuted and is not already as wanted. The
/// view is then valid until `scratch` is next changed. Keys are the same key
/// when neither is before the other under the comparator (no operator== is
/// needed); the natural order of a sort_key (std::less<>) is sorted as
/// sort_keys does, where two keys are the same key when they spell the same
/// unsigned integer, as sort_unique_keys takes it. Any other order is sorted by
/// vm::sort with the comparator, which a comparator with state has to be passed
/// in for.
template <key_sort_algo Algo = key_sort_algo::pdq, bool Unique = false, typename T, typename Comparator, typename Scratch>
requires(
    ( sort_key<std::remove_const_t<T>> && std::same_as<Comparator, std::less<>> ) ||
    ( Algo == key_sort_algo::pdq && std::copy_constructible<std::remove_const_t<T>> )
)
[[ nodiscard ]] attributed_span<std::remove_const_t<T> const, Comparator> sorted_view( attributed_span<T, Comparator> const keys, Scratch & scratch PSI_VM_LIFETIMEBOUND, Comparator const & comp = {} )
{
    using key_t   = std::remove_const_t<T>;
    using result  = attributed_span<key_t const, Comparator>;
    constexpr bool natural_keys{ sort_key<key_t> && std::same_as<Comparator, std::less<>> };
    constexpr span_facts facts{ .sorted = true, .unique = Unique };

    [[ maybe_unused ]] auto const same_key{ [ & ]( key_t const & a, key_t const & b )
    {
        if constexpr ( natural_keys )
        {
            using uint_t = key_sort_detail::key_uint_t<key_t>;
            return std::bit_cast<uint_t>( a ) == std::bit_cast<uint_t>( b );
        }
        else
            return !comp( a, b ) && !comp( b, a );
    } };
    // The sorts: the key sort for the natural order of the keys that have one,
    // the general one for the rest. Both leave the first (returned count)
    // distinct keys in front.
    [[ maybe_unused ]] auto const sort{ [ & ]( std::span<key_t> const range )
    {
        if constexpr ( natural_keys )
            sort_keys<Algo>( range );
        else
            vm::sort( range.begin(), range.end(), comp );
    } };
    [[ maybe_unused ]] auto const sort_unique{ [ & ]( std::span<key_t> const range )
    {
        if constexpr ( natural_keys )
            return sort_unique_keys<Algo>( range );
        else
            return static_cast<std::size_t>( vm::sort_unique( range.begin(), range.end(), comp ) - range.begin() );
    } };

    if ( keys.is_sorted() && ( !Unique || keys.is_unique() ) )
        return result{ std::span<key_t const>{ keys }, facts, comp };

    if constexpr ( !std::is_const_v<T> )
    {
        if ( keys.may_permute() )
        {
            std::span<T> const mutable_keys{ keys.permutable_span() };
            if constexpr ( Unique )
            {
                auto const kept{ keys.is_sorted()
                    ? static_cast<std::size_t>( std::unique( mutable_keys.begin(), mutable_keys.end(), same_key ) - mutable_keys.begin() )
                    : sort_unique( mutable_keys ) };
                return result{ std::span<key_t const>{ mutable_keys.data(), kept }, facts, comp };
            }
            else
            {
                sort( mutable_keys );
                return result{ std::span<key_t const>{ mutable_keys }, facts, comp };
            }
        }
    }

    if constexpr ( Unique )
    {
        if ( keys.is_sorted() )
        {
            // Only the repeated keys are left to remove.
            scratch.clear();
            std::unique_copy( keys.begin(), keys.end(), std::back_inserter( scratch ), same_key );
            return result{ std::span<key_t const>{ scratch.data(), scratch.size() }, facts, comp };
        }
    }

    scratch.assign( keys.begin(), keys.end() );
    if constexpr ( Unique )
    {
        auto const kept{ sort_unique( std::span<key_t>{ scratch.data(), scratch.size() } ) };
        scratch.resize( kept );
    }
    else
        sort( std::span<key_t>{ scratch.data(), scratch.size() } );
    return result{ std::span<key_t const>{ scratch.data(), scratch.size() }, facts, comp };
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
