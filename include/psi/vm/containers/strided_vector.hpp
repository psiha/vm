////////////////////////////////////////////////////////////////////////////////
///
/// \file strided_vector.hpp
/// ------------------------
///
/// Strided vector container: a vector whose "entry" is a runtime-sized
/// contiguous block (stride) of `T`. Entries are stored flat in a single
/// contiguous buffer (a `psi::vm::vector<Storage, Growth>`) and exposed via
/// `std::span<T>` proxies.
///
/// It is essentially `std::vector<std::array<T, N>>` where `N` is a
/// runtime-chosen constant. The public API mirrors std::vector as closely as
/// the proxy-reference shape allows (operator[] / at / front / back / begin /
/// end / push_back / emplace_back / pop_back / insert / erase / resize /
/// reserve / capacity / shrink_to_fit / clear / swap / comparisons).
///
/// Template parameters:
///   - T        : element type carried inside each entry
///   - StrideT  : unsigned integer type for the stride (default uint8_t)
///   - Storage  : VectorStorage contract implementation
///                (default: heap_storage<T>)
///   - Growth   : geometric-growth policy (default: 1.5x)
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

#include <psi/vm/containers/growth_policy.hpp>
#include <psi/vm/containers/heap_vector.hpp>
#include <psi/vm/containers/storage/heap.hpp>
#include <psi/vm/containers/vector.hpp>

#include <boost/assert.hpp>

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
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

namespace detail
{
    //--------------------------------------------------------------------------
    // strided_iterator
    //
    // Random-access iterator over a strided buffer. Dereference yields a
    // std::span<T> of length `stride`. The iterator is a thin wrapper around
    // (pointer, stride) — increments shift the pointer by `stride`.
    //
    // Proxy reference shape: iter_reference_t<It> == std::span<T>. The C++20
    // std::random_access_iterator concept does not require an lvalue
    // reference for `*it`, so this type is a full random-access iterator even
    // though *it returns a by-value span.
    //--------------------------------------------------------------------------
    template <typename T, typename StrideT>
    class strided_iterator
    {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept  = std::random_access_iterator_tag;
        using value_type        = std::span<T>;
        using reference         = std::span<T>;
        using difference_type   = std::ptrdiff_t;
        using pointer           = void;

        constexpr strided_iterator() noexcept = default;
        constexpr strided_iterator( T * const ptr, StrideT const stride ) noexcept
            : ptr_{ ptr }, stride_{ stride } {}

        // Conversion from iterator<T> to iterator<T const>
        template <typename U>
        requires( std::is_same_v<T, U const> )
        constexpr strided_iterator( strided_iterator<U, StrideT> const other ) noexcept
            : ptr_{ other.base() }, stride_{ other.stride() } {}

        [[ nodiscard ]] constexpr reference operator* () const noexcept { return { ptr_, stride_ }; }
        [[ nodiscard ]] constexpr reference operator[]( difference_type const n ) const noexcept { return { ptr_ + n * stride_, stride_ }; }

        constexpr strided_iterator & operator++(   ) noexcept { ptr_ += stride_; return *this; }
        constexpr strided_iterator   operator++(int) noexcept { auto tmp{ *this }; ++*this; return tmp; }
        constexpr strided_iterator & operator--(   ) noexcept { ptr_ -= stride_; return *this; }
        constexpr strided_iterator   operator--(int) noexcept { auto tmp{ *this }; --*this; return tmp; }

        constexpr strided_iterator & operator+=( difference_type const n ) noexcept { ptr_ += n * stride_; return *this; }
        constexpr strided_iterator & operator-=( difference_type const n ) noexcept { ptr_ -= n * stride_; return *this; }

        [[ nodiscard ]] friend constexpr strided_iterator operator+( strided_iterator it, difference_type const n ) noexcept { return it += n; }
        [[ nodiscard ]] friend constexpr strided_iterator operator+( difference_type const n, strided_iterator it ) noexcept { return it += n; }
        [[ nodiscard ]] friend constexpr strided_iterator operator-( strided_iterator it, difference_type const n ) noexcept { return it -= n; }

        [[ nodiscard ]] friend constexpr difference_type operator-( strided_iterator const a, strided_iterator const b ) noexcept
        {
            BOOST_ASSERT( a.stride_ == b.stride_ );
            BOOST_ASSUME( a.stride_ != 0 );
            return ( a.ptr_ - b.ptr_ ) / a.stride_;
        }

        [[ nodiscard ]] friend constexpr bool operator==( strided_iterator const a, strided_iterator const b ) noexcept { return a.ptr_ == b.ptr_; }
        [[ nodiscard ]] friend constexpr auto operator<=>( strided_iterator const a, strided_iterator const b ) noexcept { return a.ptr_ <=> b.ptr_; }

        // Non-standard accessors for cross-type comparison + const-conversion
        [[ nodiscard ]] constexpr T *     base  () const noexcept { return ptr_;    }
        [[ nodiscard ]] constexpr StrideT stride() const noexcept { return stride_; }

    private:
        T *     ptr_   { nullptr };
        StrideT stride_{ 0       };
    }; // class strided_iterator
} // namespace detail

////////////////////////////////////////////////////////////////////////////////
/// \class strided_vector
///
/// \brief Vector of fixed-extent `T` entries, where the extent (`stride`) is a
///        runtime parameter.
///
/// Each entry occupies `stride()` consecutive `T`s in a flat backing buffer
/// (`vector<Storage, Growth>`). All std::vector-like size/capacity/mutation
/// operations count in entries, not in underlying `T`s.
///
/// The stride is set at construction (or via `init(stride)` on a default-
/// constructed instance) and must not change while the container holds
/// entries. `clear()` does not reset stride.
////////////////////////////////////////////////////////////////////////////////

template <typename T,
          std::unsigned_integral StrideT = std::uint8_t,
          typename Storage               = heap_storage<T>,
          geometric_growth Growth        = geometric_growth{}>
class [[ nodiscard ]] strided_vector
{
public:
    using backing_vector_type = vector<Storage, Growth>;

    // tag so free erase/erase_if-style helpers can be constrained
    using psi_vm_strided_vector_tag = void;

    using value_type      = T;                                   // element type carried inside each entry
    using stride_type     = StrideT;                             // type of the stride
    using storage_type    = Storage;

    using size_type       = typename backing_vector_type::size_type;
    using difference_type = std::make_signed_t<size_type>;

    using       pointer   = value_type       *;
    using const_pointer   = value_type const *;

    // An "entry" is a non-owning view over `stride` consecutive Ts. There is
    // no owning entry type; the container owns all the memory flat.
    using       reference =  std::span<T      >; // proxy
    using const_reference =  std::span<T const>;

    using       iterator         = detail::strided_iterator<T      , stride_type>;
    using const_iterator         = detail::strided_iterator<T const, stride_type>;
    using       reverse_iterator = std::reverse_iterator<      iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    //--------------------------------------------------------------------------
    // Construction
    //--------------------------------------------------------------------------
    constexpr strided_vector() noexcept = default;

    // Construct empty with stride preset
    explicit constexpr strided_vector( stride_type const stride ) noexcept : stride_{ stride } {}

    // Construct `count` default-initialized entries of the given stride
    constexpr strided_vector( stride_type const stride, size_type const count )
        : data_( static_cast<size_type>( count ) * stride ), stride_{ stride }
    {}

    // Construct `count` entries initialized from a prototype span
    constexpr strided_vector( stride_type const stride, size_type const count, std::span<T const> const prototype )
        : stride_{ stride }
    {
        BOOST_ASSERT( prototype.size() == stride );
        data_.reserve( count * stride );
        for ( size_type i{ 0 }; i < count; ++i )
            data_.append_range( prototype );
    }

    // Construct from a range of spans (each of length stride)
    template <std::ranges::input_range EntryRng>
    requires std::convertible_to<std::ranges::range_reference_t<EntryRng>, std::span<T const>>
    constexpr strided_vector( stride_type const stride, EntryRng && entries ) : stride_{ stride }
    {
        if constexpr ( std::ranges::sized_range<EntryRng> )
            data_.reserve( static_cast<size_type>( std::ranges::size( entries ) ) * stride );
        for ( std::span<T const> const e : entries )
            push_back( e );
    }

    //--------------------------------------------------------------------------
    // Stride control
    //--------------------------------------------------------------------------

    /// Reset the stride. Requires the container to be empty (or becomes empty
    /// as a side effect). Does not release backing storage.
    void init( stride_type const s ) noexcept
    {
        stride_ = s;
        clear();
    }

    [[ nodiscard ]] stride_type stride() const noexcept { return stride_; }

    //--------------------------------------------------------------------------
    // Storage extraction / adoption
    //
    // Allow the backing buffer to be moved out for reuse (cursor walks,
    // external sorting, etc.) and moved back in later. Entry count resets to
    // zero on adopt.
    //--------------------------------------------------------------------------
    [[ nodiscard ]] backing_vector_type extractData() noexcept { return std::move( data_ ); }

    void adoptData( backing_vector_type && v ) noexcept
    {
        data_ = std::move( v );
        data_.clear();
    }

    //--------------------------------------------------------------------------
    // Capacity
    //--------------------------------------------------------------------------
    [[ nodiscard ]] bool empty() const noexcept { return data_.empty(); }

    [[ nodiscard ]] size_type size() const noexcept
    {
        return stride_ > 0 ? static_cast<size_type>( data_.size() / stride_ ) : size_type{ 0 };
    }

    [[ nodiscard ]] size_type capacity() const noexcept
    {
        return stride_ > 0 ? static_cast<size_type>( data_.capacity() / stride_ ) : size_type{ 0 };
    }

    [[ nodiscard ]] static constexpr size_type max_size() noexcept
    {
        // Backing vector reports max in raw `T`s; caller divides by current
        // stride for a meaningful "entry count" cap.
        return backing_vector_type::max_size();
    }

    /// Max entries for the current stride.
    [[ nodiscard ]] std::size_t maxEntries() const noexcept
    {
        return stride_ > 0 ? static_cast<std::size_t>( data_.max_size() ) / stride_ : 0;
    }

    /// Capacity measured in bytes (useful for memory-accounting diagnostics).
    [[ nodiscard ]] std::size_t capacityBytes() const noexcept
    {
        return static_cast<std::size_t>( data_.capacity() ) * sizeof( T );
    }

    void reserve       ( size_type const numEntries ) noexcept { data_.reserve( numEntries * stride_ ); }
    void shrink_to_fit (                             ) noexcept { data_.shrink_to_fit(); }

    //--------------------------------------------------------------------------
    // Element access (span-based — each "entry" is a span of length stride)
    //--------------------------------------------------------------------------
    [[ nodiscard ]] reference operator[]( size_type const i ) noexcept
    {
        return { data_.data() + static_cast<std::size_t>( i ) * stride_, stride_ };
    }

    [[ nodiscard ]] const_reference operator[]( size_type const i ) const noexcept
    {
        return { data_.data() + static_cast<std::size_t>( i ) * stride_, stride_ };
    }

    [[ nodiscard ]] reference at( size_type const i )
    {
        if ( i >= size() )
            throw std::out_of_range{ "psi::vm::strided_vector::at" };
        return ( *this )[ i ];
    }

    [[ nodiscard ]] const_reference at( size_type const i ) const
    {
        if ( i >= size() )
            throw std::out_of_range{ "psi::vm::strided_vector::at" };
        return ( *this )[ i ];
    }

    [[ nodiscard ]] reference       front()       noexcept { BOOST_ASSERT( !empty() ); return ( *this )[ 0 ]; }
    [[ nodiscard ]] const_reference front() const noexcept { BOOST_ASSERT( !empty() ); return ( *this )[ 0 ]; }
    [[ nodiscard ]] reference       back ()       noexcept { BOOST_ASSERT( !empty() ); return ( *this )[ size() - 1 ]; }
    [[ nodiscard ]] const_reference back () const noexcept { BOOST_ASSERT( !empty() ); return ( *this )[ size() - 1 ]; }

    //--------------------------------------------------------------------------
    // Raw data access
    //--------------------------------------------------------------------------
    [[ nodiscard ]]       pointer data()       noexcept { return data_.data(); }
    [[ nodiscard ]] const_pointer data() const noexcept { return data_.data(); }

    /// Access the backing `vector<Storage, Growth>` (read-only) — handy when
    /// passing the flat buffer to algorithms that don't need stride awareness.
    [[ nodiscard ]] backing_vector_type const & backing() const noexcept { return data_; }

    //--------------------------------------------------------------------------
    // Iterators
    //--------------------------------------------------------------------------
    [[ nodiscard ]]       iterator  begin()       noexcept { return       iterator{ data_.data()               , stride_ }; }
    [[ nodiscard ]] const_iterator  begin() const noexcept { return const_iterator{ data_.data()               , stride_ }; }
    [[ nodiscard ]] const_iterator cbegin() const noexcept { return begin(); }
    [[ nodiscard ]]       iterator  end  ()       noexcept { return       iterator{ data_.data() + data_.size(), stride_ }; }
    [[ nodiscard ]] const_iterator  end  () const noexcept { return const_iterator{ data_.data() + data_.size(), stride_ }; }
    [[ nodiscard ]] const_iterator cend  () const noexcept { return end(); }

    [[ nodiscard ]]       reverse_iterator  rbegin()       noexcept { return       reverse_iterator{ end  () }; }
    [[ nodiscard ]] const_reverse_iterator  rbegin() const noexcept { return const_reverse_iterator{ end  () }; }
    [[ nodiscard ]] const_reverse_iterator crbegin() const noexcept { return rbegin(); }
    [[ nodiscard ]]       reverse_iterator  rend  ()       noexcept { return       reverse_iterator{ begin() }; }
    [[ nodiscard ]] const_reverse_iterator  rend  () const noexcept { return const_reverse_iterator{ begin() }; }
    [[ nodiscard ]] const_reverse_iterator crend  () const noexcept { return rend  (); }

    //--------------------------------------------------------------------------
    // Modifiers
    //--------------------------------------------------------------------------
    void clear() noexcept { data_.clear(); }

    /// Ensure at least `additionalEntries` more entries fit without
    /// reallocation. Geometric growth (1.5x or +256 entries, whichever is
    /// larger) so a sequence of push_back calls is amortized O(1).
    void ensureCapacity( size_type const additionalEntries = 1 ) noexcept
    {
        auto const newSize{ static_cast<std::size_t>( data_.size() )
                          + static_cast<std::size_t>( additionalEntries ) * stride_ };
        if ( newSize > data_.capacity() )
        {
            static constexpr std::size_t minGrowth{ 256 };
            auto const growth  { std::max<std::size_t>( data_.capacity() / 2, minGrowth ) };
            auto const reserved{ std::max<std::size_t>( newSize, data_.capacity() + growth ) };
            data_.reserve( static_cast<size_type>( reserved ) );
        }
    }

    /// Append a single entry. The source span must be exactly `stride`
    /// elements long.
    void push_back( std::span<T const> const entry )
    {
        BOOST_ASSERT( entry.size() == stride_ );
        ensureCapacity();
        data_.append_range( entry );
    }

    /// Append an entry filled with a single value.
    void push_back_fill( T const value )
    {
        ensureCapacity();
        data_.resize( data_.size() + stride_, value );
    }

    /// Construct an entry in place from `stride` scalar arguments.
    /// For proper amortized-O(1) growth the arguments are copied in after
    /// growing the backing vector with geometric headroom.
    template <typename... Args>
    requires( sizeof...( Args ) > 0 && ( std::convertible_to<Args, T> && ... ) )
    reference emplace_back( Args &&... args )
    {
        BOOST_ASSERT( sizeof...( Args ) == stride_ );
        ensureCapacity();
        T const src[]{ static_cast<T>( std::forward<Args>( args ) )... };
        data_.append_range( std::span<T const>{ src, sizeof...( Args ) } );
        return back();
    }

    /// Remove the last entry (requires !empty()).
    void pop_back() noexcept
    {
        BOOST_ASSERT( !empty() );
        data_.shrink_by( stride_ );
    }

    /// Insert a single entry at `pos`. Returns iterator to the inserted entry.
    iterator insert( const_iterator const pos, std::span<T const> const entry )
    {
        BOOST_ASSERT( entry.size() == stride_ );
        auto const offset  { static_cast<size_type>( pos - cbegin() ) };
        auto const byte_pos{ data_.cbegin() + static_cast<difference_type>( offset ) * stride_ };
        data_.insert( byte_pos, entry.begin(), entry.end() );
        return begin() + static_cast<difference_type>( offset );
    }

    /// Insert `count` copies of a prototype entry at `pos`.
    iterator insert( const_iterator const pos, size_type const count, std::span<T const> const prototype )
    {
        BOOST_ASSERT( prototype.size() == stride_ );
        auto const offset{ static_cast<size_type>( pos - cbegin() ) };
        for ( size_type i{ 0 }; i < count; ++i )
            insert( cbegin() + static_cast<difference_type>( offset + i ), prototype );
        return begin() + static_cast<difference_type>( offset );
    }

    /// Erase a single entry. Returns iterator to the element after the erased.
    iterator erase( const_iterator const pos ) noexcept
    {
        BOOST_ASSERT( pos >= cbegin() && pos < cend() );
        auto const offset  { static_cast<size_type>( pos - cbegin() ) };
        auto const byte_pos{ data_.cbegin() + static_cast<difference_type>( offset ) * stride_ };
        data_.erase( byte_pos, byte_pos + stride_ );
        return begin() + static_cast<difference_type>( offset );
    }

    /// Erase a range of entries [first, last).
    iterator erase( const_iterator const first, const_iterator const last ) noexcept
    {
        BOOST_ASSERT( first <= last );
        auto const offset_f{ static_cast<size_type>( first - cbegin() ) };
        auto const offset_l{ static_cast<size_type>( last  - cbegin() ) };
        auto const byte_f  { data_.cbegin() + static_cast<difference_type>( offset_f ) * stride_ };
        auto const byte_l  { data_.cbegin() + static_cast<difference_type>( offset_l ) * stride_ };
        data_.erase( byte_f, byte_l );
        return begin() + static_cast<difference_type>( offset_f );
    }

    /// Resize to `count` entries. New entries are default-initialized.
    void resize( size_type const count )
    {
        data_.resize( count * stride_ );
    }

    /// Resize to `count` entries. New entries are filled from `prototype`.
    void resize( size_type const count, std::span<T const> const prototype )
    {
        BOOST_ASSERT( prototype.size() == stride_ );
        auto const old_entries{ size() };
        if ( count <= old_entries )
        {
            data_.resize( count * stride_ );
        }
        else
        {
            data_.reserve( count * stride_ );
            for ( size_type i{ old_entries }; i < count; ++i )
                data_.append_range( prototype );
        }
    }

    void swap( strided_vector & other ) noexcept
    {
        using std::swap;
        data_.swap( other.data_ );
        swap( stride_, other.stride_ );
    }

    friend void swap( strided_vector & a, strided_vector & b ) noexcept { a.swap( b ); }

    //--------------------------------------------------------------------------
    // Comparison operators
    //
    // Two strided_vectors compare equal iff they have the same stride, size
    // and element-wise-equal contents. Ordering is lexicographic on the flat
    // buffer, with stride acting as a tiebreaker only when buffers agree
    // (i.e. container of empty stride compares less than container of
    // non-empty stride when both are empty).
    //--------------------------------------------------------------------------
    [[ nodiscard ]] friend constexpr bool operator==( strided_vector const & a, strided_vector const & b ) noexcept
    {
        return a.stride_ == b.stride_ && std::ranges::equal( a.data_, b.data_ );
    }

    [[ nodiscard ]] friend constexpr auto operator<=>( strided_vector const & a, strided_vector const & b ) noexcept
    {
        if ( auto const c{ std::lexicographical_compare_three_way( a.data_.begin(), a.data_.end(), b.data_.begin(), b.data_.end() ) }; c != 0 )
            return c;
        return a.stride_ <=> b.stride_;
    }

private:
    backing_vector_type data_;
    stride_type         stride_{ 0 };
}; // class strided_vector

//------------------------------------------------------------------------------
// Free erase / erase_if helpers (std::erase / std::erase_if analogues)
//------------------------------------------------------------------------------

template <typename SV, typename Pred>
requires requires { typename SV::psi_vm_strided_vector_tag; }
constexpr typename SV::size_type erase_if( SV & c, Pred pred )
{
    auto const keep_begin{ c.begin() };
    auto       write     { keep_begin };
    for ( auto read{ keep_begin }; read != c.end(); ++read )
    {
        if ( !pred( *read ) )
        {
            if ( write != read )
                std::ranges::copy( *read, ( *write ).begin() );
            ++write;
        }
    }
    auto const n{ static_cast<typename SV::size_type>( c.end() - write ) };
    c.erase( write, c.end() );
    return n;
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
