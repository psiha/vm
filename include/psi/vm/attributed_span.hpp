////////////////////////////////////////////////////////////////////////////////
/// psi::vm::attributed_span -- a span that carries what its producer knows
/// about the elements it views, so that consumers need not template on it.
///
/// The facts a producer states are about the sequence and what the consumer
/// may do with it:
///  * sorted      -- non-descending under the span's Comparator (std::less<>
///                   by default, i.e. operator<);
///  * unique      -- no two elements are equivalent under it (with sorted:
///                   strictly ascending);
///  * mutability  -- a level: immutable; permutable (the consumer may reorder
///                   the elements, the producer does not depend on their order
///                   any more); destroyable (the consumer may also leave the
///                   elements with any value, move from them: an rvalue view).
///                   Const elements are always immutable.
/// They are claims, not checks: a debug build verifies the ones it can in the
/// constructor, a release build trusts them. A consumer that cannot honour a
/// fact simply ignores it - every fact only ever licenses more work to be
/// skipped or done in place, never less.
///
/// The Comparator is part of the type, so that 'sorted' always says by what: a
/// span sorted by an index's key order is not one sorted by operator<. A span
/// converts to one of another comparator, keeping only what is independent of
/// the order (the mutability). The
/// debug verification needs the comparator: a stateless one (std::is_empty,
/// default constructible) is made for the purpose, any other has to be passed
/// in to the constructors that state facts - and is ignored by a release build,
/// which neither stores nor calls it.
///
/// The compile-time spellings of the same facts are the sorted_unique_t and
/// sorted_equivalent_t tags of the flat containers, and sorted_range below:
/// sorted_range converts to an attributed_span.
///
/// A span of two words: the size is 32 bits, the other half of the second word
/// holds the facts, a byte each. Converts implicitly to (and from) std::span,
/// losing (or not having) the facts.
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

#include <psi/vm/checked_iterator.hpp>

#include <psi/build/attributes.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp> // BOOST_ASSUME

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

/// What a consumer may do with the elements, in increasing order of license.
enum struct span_mutability : std::uint8_t
{
    immutable,
    permutable,
    destroyable,
};

/// The facts about a sequence, one byte each. Facts about the same elements
/// combine as the weakest of them: `a & b` keeps a fact only where both state
/// it (and the lower of the two mutability levels).
struct span_facts
{
    bool            sorted    { false };
    bool            unique    { false };
    span_mutability mutability{ span_mutability::immutable };

    friend constexpr bool operator==( span_facts const &, span_facts const & ) noexcept = default;

    constexpr span_facts & operator&=( span_facts const other ) noexcept
    {
        sorted     &= other.sorted;
        unique     &= other.unique;
        mutability  = std::min( mutability, other.mutability );
        return *this;
    }
    [[ nodiscard ]] friend constexpr span_facts operator&( span_facts a, span_facts const b ) noexcept { return a &= b; }
};

/// Whether a comparator can be made for the verification of a claim by itself.
template <typename Comparator>
inline constexpr bool self_verifying_comparator{ std::is_empty_v<Comparator> && std::default_initializable<Comparator> };

namespace detail
{
    //! The debug-build check of the claims about [first, last) under comp.
    template <typename Iterator, typename Comparator>
    constexpr void verify_facts( [[ maybe_unused ]] Iterator const first, [[ maybe_unused ]] Iterator const last, [[ maybe_unused ]] span_facts const facts, [[ maybe_unused ]] Comparator const & comp ) noexcept
    {
#   ifndef NDEBUG
        if constexpr ( std::predicate<Comparator const &, std::iter_reference_t<Iterator>, std::iter_reference_t<Iterator>> )
        {
            if ( facts.sorted )
            {
                BOOST_ASSERT_MSG( std::is_sorted( first, last, comp ), "claimed to be sorted" );
                if ( facts.unique )
                {
                    BOOST_ASSERT_MSG( std::adjacent_find( first, last, [ & ]( auto const & a, auto const & b ) { return !comp( a, b ); } ) == last, "claimed to be sorted and unique" );
                }
            }
        }
#   endif
    }

    //! What sorted_range::filled() constructs: a container (held by value) of
    //! contiguous elements, made by its sized constructor, that the filler
    //! writes through a span.
    template <typename Range, typename Filler>
    concept fillable_container =
        !std::is_reference_v<Range>                                               &&
        std::ranges::contiguous_range<Range>                                      &&
        std::ranges::sized_range<Range>                                           &&
        std::constructible_from<Range, std::ranges::range_size_t<Range>>          &&
        std::invocable<Filler, std::span<std::ranges::range_value_t<Range>>>;
} // namespace detail

template <typename T, typename Comparator = std::less<>>
class attributed_span;

template <typename>
inline constexpr bool is_attributed_span_v{ false };
template <typename T, typename Comparator>
inline constexpr bool is_attributed_span_v<attributed_span<T, Comparator>>{ true };

template <std::ranges::range Range, bool Unique = false, typename Comparator = std::less<>>
class sorted_range;

/// A sorted_range by the Comparator: what states its order to an
/// attributed_span of that comparator (and so is not taken as a plain range by
/// it, whatever its value category).
template <typename, typename>
inline constexpr bool is_sorted_range_by_v{ false };
template <typename Range, bool Unique, typename Comparator>
inline constexpr bool is_sorted_range_by_v<sorted_range<Range, Unique, Comparator>, Comparator>{ true };


/// A gsl::Pointer (a view of elements it does not own), so clang warns, as it
/// does for std::span, when one is made from a temporary gsl::Owner (a standard
/// container) and outlives it, or is returned viewing a local.
template <typename T, typename Comparator>
class [[ clang::trivial_abi, gsl::Pointer( T ) ]] attributed_span
{
public:
    using element_type     = T;
    using value_type       = std::remove_cv_t<T>;
    using comparator_type  = Comparator;
    using size_type        = std::uint32_t;
    using difference_type  = std::ptrdiff_t;
    using pointer          = T *;
    using reference        = T &;
    using iterator         = detail::checked_iterator<T>;
    using reverse_iterator = std::reverse_iterator<iterator>;

    constexpr attributed_span() noexcept = default;

    // With the comparator the claims are verified by it (debug builds only).
    constexpr attributed_span( T * const first PSI_LIFETIMEBOUND, size_type const size, span_facts const facts, Comparator const & comp ) noexcept
        : attributed_span( first, size, facts, unchecked )
    {
        detail::verify_facts( first, first + size, facts_, comp );
    }
    constexpr attributed_span( T * const first PSI_LIFETIMEBOUND, size_type const size, span_facts const facts = {} ) noexcept
    requires( self_verifying_comparator<Comparator> )
        : attributed_span( first, size, facts, Comparator{} ) {}

    constexpr attributed_span( std::span<T> const span, span_facts const facts, Comparator const & comp ) noexcept
        : attributed_span( span.data(), checked_size( span.size() ), facts, comp ) {}
    constexpr attributed_span( std::span<T> const span, span_facts const facts = {} ) noexcept
    requires( self_verifying_comparator<Comparator> )
        : attributed_span( span, facts, Comparator{} ) {}
    /// No facts: nothing to verify, whatever the comparator.
    constexpr attributed_span( std::span<T> const span ) noexcept
    requires( !self_verifying_comparator<Comparator> )
        : attributed_span( span.data(), checked_size( span.size() ), span_facts{}, unchecked ) {}

    template <typename Range>
    requires( !is_attributed_span_v<std::remove_cvref_t<Range>> && !std::same_as<std::remove_cvref_t<Range>, std::span<T>> && !is_sorted_range_by_v<std::remove_cvref_t<Range>, Comparator> && std::is_constructible_v<std::span<T>, Range> )
    constexpr attributed_span( Range && range, span_facts const facts, Comparator const & comp ) noexcept
        : attributed_span( std::span<T>( std::forward<Range>( range ) ), facts, comp ) {}
    template <typename Range>
    requires( !is_attributed_span_v<std::remove_cvref_t<Range>> && !std::same_as<std::remove_cvref_t<Range>, std::span<T>> && !is_sorted_range_by_v<std::remove_cvref_t<Range>, Comparator> && std::is_constructible_v<std::span<T>, Range> && self_verifying_comparator<Comparator> )
    constexpr attributed_span( Range && range, span_facts const facts = {} ) noexcept
        : attributed_span( std::span<T>( std::forward<Range>( range ) ), facts, Comparator{} ) {}
    template <typename Range>
    requires( !is_attributed_span_v<std::remove_cvref_t<Range>> && !std::same_as<std::remove_cvref_t<Range>, std::span<T>> && !is_sorted_range_by_v<std::remove_cvref_t<Range>, Comparator> && std::is_constructible_v<std::span<T>, Range> && !self_verifying_comparator<Comparator> )
    constexpr attributed_span( Range && range ) noexcept
        : attributed_span( std::span<T>( std::forward<Range>( range ) ) ) {}

    /// A range that states it is sorted (and unique) passes that on; it has
    /// verified it already. A sorted_range that holds the elements (its range
    /// is not borrowed: a container by value) binds the span to its lifetime;
    /// one that refers to them (a reference, a view) does not.
    template <typename Range, bool Unique>
    requires( std::is_constructible_v<std::span<T>, Range const &> && std::ranges::borrowed_range<Range> )
    constexpr attributed_span( sorted_range<Range, Unique, Comparator> const & range ) noexcept
        : attributed_span( std::span<T>( range.base() ).data(), checked_size( std::span<T>( range.base() ).size() ), span_facts{ .sorted = true, .unique = Unique }, unchecked ) {}
    template <typename Range, bool Unique>
    requires( std::is_constructible_v<std::span<T>, Range const &> && !std::ranges::borrowed_range<Range> )
    constexpr attributed_span( sorted_range<Range, Unique, Comparator> const & range PSI_LIFETIMEBOUND ) noexcept
        : attributed_span( std::span<T>( range.base() ).data(), checked_size( std::span<T>( range.base() ).size() ), span_facts{ .sorted = true, .unique = Unique }, unchecked ) {}

    /// From a span of elements that convert (T to T const): the facts about
    /// what may be done with the elements do not survive the const.
    template <typename U>
    requires( !std::same_as<U, T> && std::is_convertible_v<U( * )[], T( * )[]> )
    constexpr attributed_span( attributed_span<U, Comparator> const other ) noexcept
        : attributed_span( other.data(), other.size(), other.facts(), unchecked ) {}

    /// From a span with another comparator, with the warrants that do not depend
    /// on it: what may be done with the elements. Nothing about their order
    /// survives, not even uniqueness: elements that are not equivalent by one
    /// comparator may be by the other.
    template <typename U, typename OtherComparator>
    requires( !std::same_as<OtherComparator, Comparator> && std::is_convertible_v<U( * )[], T( * )[]> )
    constexpr attributed_span( attributed_span<U, OtherComparator> const other ) noexcept
        : attributed_span( other.data(), other.size(), span_facts{ .mutability = other.facts().mutability }, unchecked ) {}

    [[ nodiscard, gnu::pure ]] constexpr operator std::span<T>() const noexcept { return { data_, size_ }; }

    [[ nodiscard, gnu::pure ]] constexpr span_facts facts() const noexcept { return facts_; }

    [[ nodiscard, gnu::pure ]] constexpr bool is_sorted  () const noexcept { return facts_.sorted; }
    [[ nodiscard, gnu::pure ]] constexpr bool is_unique  () const noexcept { return facts_.unique; }
    [[ nodiscard, gnu::pure ]] constexpr bool may_permute() const noexcept { return facts_.mutability >= span_mutability::permutable ; }
    [[ nodiscard, gnu::pure ]] constexpr bool may_destroy() const noexcept { return facts_.mutability >= span_mutability::destroyable; }

    /// The elements as a span that may be reordered / left with any value: the
    /// producer stated it does not depend on their order / their values. The
    /// plain std::span<T> conversion is for reading and for writing that keeps
    /// the producer's expectations (the level is `immutable` unless stated), so
    /// it cannot assume anything; a consumer that reorders or consumes the
    /// elements takes them from here, which checks (in debug builds) and
    /// assumes (in release builds) that the level allows it.
    [[ nodiscard, gnu::pure ]] constexpr std::span<T> permutable_span() const noexcept requires( !std::is_const_v<T> )
    {
        BOOST_ASSUME( facts_.mutability >= span_mutability::permutable );
        return { data_, size_ };
    }
    [[ nodiscard, gnu::pure ]] constexpr std::span<T> destroyable_span() const noexcept requires( !std::is_const_v<T> )
    {
        BOOST_ASSUME( facts_.mutability >= span_mutability::destroyable );
        return { data_, size_ };
    }

    /// The same elements with only the facts that `limit` states as well kept.
    [[ nodiscard, gnu::pure ]] constexpr attributed_span restricted( span_facts const limit ) const noexcept
    {
        return { data_, size_, facts_ & limit, unchecked };
    }

    [[ nodiscard, gnu::pure ]] constexpr T *               data  () const noexcept { return data_; }
    [[ nodiscard, gnu::pure ]] constexpr size_type         size  () const noexcept { return size_; }
    [[ nodiscard, gnu::pure ]] constexpr bool              empty () const noexcept { return size_ == 0; }
    [[ nodiscard, gnu::pure ]] constexpr iterator          begin () const noexcept { return detail::make_checked_iterator( data_, data_, data_ + size_ ); }
    [[ nodiscard, gnu::pure ]] constexpr iterator          end   () const noexcept { return detail::make_checked_iterator( data_ + size_, data_, data_ + size_ ); }
    [[ nodiscard, gnu::pure ]] constexpr reverse_iterator  rbegin() const noexcept { return reverse_iterator{ end  () }; }
    [[ nodiscard, gnu::pure ]] constexpr reverse_iterator  rend  () const noexcept { return reverse_iterator{ begin() }; }
    [[ nodiscard, gnu::pure ]] constexpr T &               front () const noexcept { BOOST_ASSERT( !empty() ); return data_[ 0 ]; }
    [[ nodiscard, gnu::pure ]] constexpr T &               back  () const noexcept { BOOST_ASSERT( !empty() ); return data_[ size_ - 1 ]; }
    [[ nodiscard, gnu::pure ]] constexpr T &               operator[]( size_type const index ) const noexcept { BOOST_ASSERT( index < size_ ); return data_[ index ]; }

    /// Any run of the elements keeps every fact: a run of sorted elements is
    /// sorted, of unique ones unique, and what may be done with all of the
    /// elements may be done with some.
    [[ nodiscard, gnu::pure ]] constexpr attributed_span first( size_type const count ) const noexcept { BOOST_ASSERT( count <= size_ ); return { data_, count, facts_, unchecked }; }
    [[ nodiscard, gnu::pure ]] constexpr attributed_span last ( size_type const count ) const noexcept { BOOST_ASSERT( count <= size_ ); return { data_ + ( size_ - count ), count, facts_, unchecked }; }
    [[ nodiscard, gnu::pure ]] constexpr attributed_span subspan( size_type const offset, size_type const count ) const noexcept
    {
        BOOST_ASSERT( offset <= size_ && count <= size_ - offset );
        return { data_ + offset, count, facts_, unchecked };
    }
    [[ nodiscard, gnu::pure ]] constexpr attributed_span subspan( size_type const offset ) const noexcept { return subspan( offset, size_ - offset ); }

private:
    struct unchecked_t {};
    static constexpr unchecked_t unchecked{};

    // The facts are taken as they are (they have been verified, or follow from
    // facts that have been).
    constexpr attributed_span( T * const first, size_type const size, span_facts const facts, unchecked_t ) noexcept
        : data_{ first }, size_{ size }, facts_{ normalized( facts ) } {}

    [[ nodiscard ]] static constexpr span_facts normalized( span_facts const facts ) noexcept
    {
        if constexpr ( std::is_const_v<T> )
            return facts & span_facts{ .sorted = true, .unique = true };
        else
            return facts;
    }

    [[ nodiscard ]] static constexpr size_type checked_size( std::size_t const size ) noexcept
    {
        BOOST_ASSERT( size <= std::numeric_limits<size_type>::max() );
        return static_cast<size_type>( size );
    }

private:
    T *        data_ { nullptr };
    size_type  size_ { 0 };
    span_facts facts_{};

    template <typename, typename> friend class attributed_span;
}; // class attributed_span

template <typename T>
attributed_span( std::span<T>, span_facts = {} ) -> attributed_span<T>;
template <typename T, typename Comparator>
attributed_span( std::span<T>, span_facts, Comparator ) -> attributed_span<T, Comparator>;
template <std::ranges::contiguous_range Range>
attributed_span( Range &, span_facts = {} ) -> attributed_span<std::remove_reference_t<std::ranges::range_reference_t<Range>>>;
template <std::ranges::contiguous_range Range, typename Comparator>
attributed_span( Range &, span_facts, Comparator ) -> attributed_span<std::remove_reference_t<std::ranges::range_reference_t<Range>>, Comparator>;

static_assert( sizeof( void * ) != 8 || sizeof( attributed_span<int> ) == 16 );
static_assert( std::is_trivially_copyable_v<attributed_span<int>> );
static_assert( sizeof( span_facts ) == 3 );


/// The static spelling of 'sorted' (and, with Unique, 'sorted and unique') by
/// the Comparator: a strong alias that a range is, wherever it can be passed as
/// one to a function that wants to know. Holds the range as given (a view by
/// value, a container as a reference to it), so it can be a parameter or a
/// return type without copying anything. A debug build checks the claim (a
/// comparator that is not self verifying has to be passed in for that).
template <std::ranges::range Range, bool Unique, typename Comparator>
class sorted_range
{
public:
    static constexpr bool unique{ Unique };

    constexpr explicit sorted_range( Range range, Comparator const & comp ) noexcept( std::is_nothrow_move_constructible_v<Range> )
        : range_( std::forward<Range>( range ) )
    {
        detail::verify_facts( std::ranges::begin( range_ ), std::ranges::end( range_ ), span_facts{ .sorted = true, .unique = Unique }, comp );
    }
    constexpr explicit sorted_range( Range range ) noexcept( std::is_nothrow_move_constructible_v<Range> )
    requires( self_verifying_comparator<Comparator> )
        : sorted_range( std::forward<Range>( range ), Comparator{} ) {}

    [[ nodiscard, gnu::pure ]] constexpr auto begin() const { return std::ranges::begin( range_ ); }
    [[ nodiscard, gnu::pure ]] constexpr auto end  () const { return std::ranges::end  ( range_ ); }
    [[ nodiscard, gnu::pure ]] constexpr auto size () const requires std::ranges::sized_range<Range const> { return std::ranges::size( range_ ); }
    [[ nodiscard, gnu::pure ]] constexpr bool empty() const { return std::ranges::empty( range_ ); }

    /// Makes the container (of `size` elements, by its sized constructor) in
    /// place, hands the filler the span over its elements - their final place,
    /// nothing is copied or moved after the filler returns - and then checks
    /// the claim (debug builds), so that a container that is built to be
    /// sorted need not be built first and wrapped (moved) after:
    ///   return sorted_range<heap_vector<std::uint32_t>, true>::filled( n, [&]( std::span<std::uint32_t> const out ) { ... } );
    /// The elements the filler gets are as the sized constructor leaves them:
    /// value-initialized by a standard container, default-initialized (so
    /// uninitialized, for a trivial element type) by a psi::vm vector. The
    /// filler has to leave them as claimed (sorted, and unique with Unique,
    /// under the comparator): a release build trusts it. A filler that throws
    /// leaves no sorted_range behind (the container is destroyed with the
    /// exception).
    template <typename Filler>
    requires( detail::fillable_container<Range, Filler> )
    [[ nodiscard ]] static constexpr sorted_range filled( std::size_t const size, Filler && filler, Comparator const & comp ) noexcept( nothrow_fillable<Filler> )
    {
        return sorted_range( filling{}, size, std::forward<Filler>( filler ), comp );
    }
    template <typename Filler>
    requires( detail::fillable_container<Range, Filler> && self_verifying_comparator<Comparator> )
    [[ nodiscard ]] static constexpr sorted_range filled( std::size_t const size, Filler && filler ) noexcept( nothrow_fillable<Filler> )
    {
        return sorted_range( filling{}, size, std::forward<Filler>( filler ), Comparator{} );
    }

    [[ nodiscard, gnu::pure ]] constexpr Range const & base() const & noexcept { return range_; }
    /// The range itself, moved out (for a consumer that goes on to reorder or
    /// change it, which ends what the claim was about).
    [[ nodiscard ]] constexpr Range base() && noexcept( std::is_nothrow_move_constructible_v<Range> ) { return std::forward<Range>( range_ ); }

private:
    template <typename Filler>
    static constexpr bool nothrow_fillable
    {
        std::is_nothrow_constructible_v<Range, std::ranges::range_size_t<Range>> &&
        std::is_nothrow_invocable_v<Filler, std::span<std::ranges::range_value_t<Range>>>
    };

    struct filling {};
    template <typename Filler>
    constexpr sorted_range( filling, std::size_t const size, Filler && filler, Comparator const & comp ) noexcept( nothrow_fillable<Filler> )
        : range_( checked_range_size( size ) )
    {
        std::invoke( std::forward<Filler>( filler ), std::span<std::ranges::range_value_t<Range>>( std::ranges::data( range_ ), std::ranges::size( range_ ) ) );
        detail::verify_facts( std::ranges::begin( range_ ), std::ranges::end( range_ ), span_facts{ .sorted = true, .unique = Unique }, comp );
    }

    // The size in the container's own size type (which it is to hold).
    [[ nodiscard ]] static constexpr auto checked_range_size( std::size_t const size ) noexcept
    {
        using size_type = std::ranges::range_size_t<Range>;
        if constexpr ( sizeof( size_type ) < sizeof( size ) )
            BOOST_ASSERT_MSG( size <= std::numeric_limits<size_type>::max(), "more elements than the container can count" );
        return static_cast<size_type>( size );
    }

    Range range_;
}; // class sorted_range

template <typename Range>
sorted_range( Range && ) -> sorted_range<Range>;
template <typename Range, typename Comparator>
sorted_range( Range &&, Comparator ) -> sorted_range<Range, false, Comparator>;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------

// Like std::span: a view (cheap to copy) that does not own its elements, so an
// iterator into a temporary one stays valid.
template <typename T, typename Comparator>
inline constexpr bool std::ranges::enable_borrowed_range<psi::vm::attributed_span<T, Comparator>>{ true };
template <typename T, typename Comparator>
inline constexpr bool std::ranges::enable_view<psi::vm::attributed_span<T, Comparator>>{ true };
