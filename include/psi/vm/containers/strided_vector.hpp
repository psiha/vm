////////////////////////////////////////////////////////////////////////////////
///
/// \file strided_vector.hpp
/// ------------------------
///
/// Strided vector container: a vector whose "entries" are fixed-length spans
/// of `stride` consecutive `T`s stored in a single contiguous heap buffer.
///
/// Use cases:
///   - coordinate keys of a multidimensional hash table
///   - multi-value rows where the field count is a runtime-chosen constant
///   - any collection of span<T, dynamic_extent> that share the same extent
///
/// Benefits over `vector<vector<T>>`:
///   - single heap allocation, cache-friendly sequential scans
///   - no per-entry size/capacity overhead
///   - span-based access with the stride known out-of-band
///
/// Growth is geometric (amortized O(1) push_back).
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

#include <psi/vm/containers/heap_vector.hpp>

#include <boost/assert.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// \class strided_vector
///
/// \brief Stride-backed vector of fixed-extent `T` entries.
///
/// Each entry occupies `stride()` consecutive elements in a flat
/// `heap_vector<T, sz_t>`. Entries are addressed by index and returned as
/// `std::span<T, dynamic_extent>` of length `stride`.
///
/// The stride is a runtime parameter supplied via `init(stride)` (or via the
/// single-argument constructor) and must not change while the container holds
/// any entries.
///
/// `sz_t` parameterizes the backing vector's size type (defaults to
/// `std::size_t`). The public `size()` return type mirrors the backing
/// vector's.
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::unsigned_integral sz_t = std::size_t>
class strided_vector
{
public:
    using value_type     = T;
    using size_type      = sz_t;
    using stride_type    = std::uint8_t;
    using backing_vector = heap_vector<T, sz_t>;

    //--------------------------------------------------------------------------
    // Construction / lifecycle
    //--------------------------------------------------------------------------
    constexpr strided_vector() noexcept = default;
    explicit constexpr strided_vector( stride_type const stride ) noexcept : stride_{ stride } {}

    void init( stride_type const stride ) noexcept { stride_ = stride; clear(); }

    void clear() noexcept { data_.clear(); }

    //--------------------------------------------------------------------------
    // Storage extraction / adoption
    //
    // Allow the backing buffer to be moved out for reuse (cursor walks,
    // external sorting, etc.) and later moved back in.
    //--------------------------------------------------------------------------
    [[ nodiscard ]] backing_vector extractData() noexcept { return std::move( data_ ); }

    void adoptData( backing_vector && v ) noexcept
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

    [[ nodiscard ]] stride_type stride() const noexcept { return stride_; }

    [[ nodiscard ]] std::size_t capacityBytes() const noexcept
    {
        return static_cast<std::size_t>( data_.capacity() ) * sizeof( T );
    }

    [[ nodiscard ]] std::size_t maxEntries() const noexcept
    {
        return stride_ > 0 ? static_cast<std::size_t>( data_.max_size() ) / stride_ : 0;
    }

    void reserve( size_type const numEntries ) noexcept
    {
        data_.reserve( numEntries * stride_ );
    }

    void shrink_to_fit() noexcept { data_.shrink_to_fit(); }

    //--------------------------------------------------------------------------
    // Element access (span-based)
    //--------------------------------------------------------------------------
    [[ nodiscard ]] std::span<T const> operator[]( size_type const i ) const noexcept
    {
        return { data_.data() + static_cast<std::size_t>( i ) * stride_, stride_ };
    }

    [[ nodiscard ]] std::span<T> operator[]( size_type const i ) noexcept
    {
        return { data_.data() + static_cast<std::size_t>( i ) * stride_, stride_ };
    }

    //--------------------------------------------------------------------------
    // Raw data access (for low-level iteration)
    //--------------------------------------------------------------------------
    [[ nodiscard ]] T const * data() const noexcept { return data_.data(); }

    //--------------------------------------------------------------------------
    // Modifiers
    //--------------------------------------------------------------------------

    /// Ensure geometric-growth capacity for at least `additionalEntries` more
    /// entries. Used internally by push_back / push_back_fill to guarantee
    /// amortized O(1) appends.
    void ensureCapacity( size_type const additionalEntries = 1 ) noexcept
    {
        auto const newSize{ data_.size() + additionalEntries * stride_ };
        if ( newSize > data_.capacity() )
        {
            // Geometric growth: at least 1.5x or `minGrowth`, whichever is
            // larger. minGrowth ensures small strided containers do not
            // thrash through tiny reallocations on the first few inserts.
            static constexpr std::size_t minGrowth{ 256 };
            auto const growth  { std::max<std::size_t>( data_.capacity() / 2, minGrowth ) };
            auto const reserved{ std::max<std::size_t>( newSize, data_.capacity() + growth ) };
            data_.reserve( static_cast<size_type>( reserved ) );
        }
    }

    /// Append a single entry (span of exactly `stride` elements).
    void push_back( std::span<T const> const entry ) noexcept
    {
        BOOST_ASSERT( entry.size() == stride_ );
        ensureCapacity();
        data_.append_range( entry );
    }

    /// Append an entry filled with a single value.
    void push_back_fill( T const value ) noexcept
    {
        ensureCapacity();
        data_.resize( data_.size() + stride_, value );
    }

    //--------------------------------------------------------------------------
    // Iteration (yields spans — one span per entry)
    //--------------------------------------------------------------------------
    struct Iterator
    {
        strided_vector * parent;
        size_type        idx;

        [[ nodiscard ]] std::span<T> operator*() const noexcept { return ( *parent )[ idx ]; }
        Iterator & operator++() noexcept { ++idx; return *this; }
        [[ nodiscard ]] bool operator!=( Iterator const & other ) const noexcept { return idx != other.idx; }
        [[ nodiscard ]] bool operator==( Iterator const & other ) const noexcept { return idx == other.idx; }
    };

    struct ConstIterator
    {
        strided_vector const * parent;
        size_type              idx;

        [[ nodiscard ]] std::span<T const> operator*() const noexcept { return ( *parent )[ idx ]; }
        ConstIterator & operator++() noexcept { ++idx; return *this; }
        [[ nodiscard ]] bool operator!=( ConstIterator const & other ) const noexcept { return idx != other.idx; }
        [[ nodiscard ]] bool operator==( ConstIterator const & other ) const noexcept { return idx == other.idx; }
    };

    [[ nodiscard ]]      Iterator begin()       noexcept { return {      this, 0      }; }
    [[ nodiscard ]]      Iterator end  ()       noexcept { return {      this, size() }; }
    [[ nodiscard ]] ConstIterator begin() const noexcept { return {      this, 0      }; }
    [[ nodiscard ]] ConstIterator end  () const noexcept { return {      this, size() }; }

private:
    backing_vector data_;
    stride_type    stride_{ 0 };
}; // class strided_vector

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
