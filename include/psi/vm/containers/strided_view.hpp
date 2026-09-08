////////////////////////////////////////////////////////////////////////////////
///
/// \file strided_view.hpp
/// --------------------
///
/// A non-owning random-access view over ONE field of every entry in an array
/// of fixed-stride entries: the transverse slice of a `strided_vector`.
///
/// `strided_vector` addresses an array of entries, each a runtime-fixed-length
/// contiguous block; iterating it walks whole entries. `strided_view` walks the
/// other axis - it addresses one field position across all of the entries,
/// stepping by the entry stride and yielding a single `T`:
///
///     strided_vector<float, 4> points( 3, count ); // xyz entries
///     strided_view<float const> xs{ points.field( 0 ) };
///     std::ranges::max( xs );                      // every entry's x, no copy
///
/// Being a plain view (a base pointer, a byte stride and an element count, two
/// machine words in total) it also addresses interleaved memory that no
/// container owns - a row-major record array whose fields differ in type and
/// width, where each field is one `strided_view` of its own type at its own byte
/// offset. That is what the byte-offset constructor is for.
///
/// A stride equal to `sizeof( T )` makes the view contiguous, in which case it
/// carries a plain data pointer and degenerates to a `std::span` - so generic
/// code can take one view type for both the interleaved and the contiguous
/// case. Prefer `std::span` where the contiguity is statically known: the
/// stride costs a multiply per subscript and blocks the vectorization a
/// contiguous span permits.
///
/// The view is a borrowed range: its iterators stay valid after it dies, as
/// they carry the base pointer and the stride themselves.
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

#include <boost/config_ex.hpp> // before <boost/assert.hpp>: Boost 1.92's assert.hpp uses BOOST_NORETURN without pulling <boost/config.hpp> itself
#include <boost/assert.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ranges>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace detail
{
    /// The byte pointer a field view of `T` walks: const-correct with `T`.
    template <typename T>
    using strided_view_byte_ptr = std::conditional_t<std::is_const_v<T>, std::byte const *, std::byte *>;

    //==========================================================================
    // strided_view_iterator — one field position, stepping by the entry stride
    //==========================================================================
    // A free class rather than a member of strided_view so that the
    // mutable->const conversion below is a deducible context.
    //--------------------------------------------------------------------------
    template <typename T>
    class strided_view_iterator
    {
    public:
        using byte_ptr          = strided_view_byte_ptr<T>;
        using stride_type       = std::uint32_t;
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept  = std::random_access_iterator_tag;
        using value_type        = std::remove_cv_t<T>;
        using difference_type   = std::ptrdiff_t;
        using reference         = T &;
        using pointer           = T *;

        constexpr strided_view_iterator() noexcept = default;
        constexpr strided_view_iterator( byte_ptr const p, stride_type const stride ) noexcept : p_{ p }, stride_{ stride } {}

        /// Conversion from strided_view_iterator<T> to strided_view_iterator<T const>
        template <typename U>
        requires( std::is_same_v<T, U const> )
        constexpr strided_view_iterator( strided_view_iterator<U> const other ) noexcept : p_{ other.base() }, stride_{ other.stride() } {}

        [[ nodiscard ]] constexpr reference operator* () const noexcept { return *reinterpret_cast<pointer>( p_ ); }
        [[ nodiscard ]] constexpr pointer   operator->() const noexcept { return  reinterpret_cast<pointer>( p_ ); }
        [[ nodiscard ]] constexpr reference operator[]( difference_type const n ) const noexcept { return *( *this + n ); }

        constexpr strided_view_iterator & operator++(   ) noexcept { p_ += stride_; return *this; }
        constexpr strided_view_iterator   operator++(int) noexcept { auto tmp{ *this }; ++*this; return tmp; }
        constexpr strided_view_iterator & operator--(   ) noexcept { p_ -= stride_; return *this; }
        constexpr strided_view_iterator   operator--(int) noexcept { auto tmp{ *this }; --*this; return tmp; }

        constexpr strided_view_iterator & operator+=( difference_type const n ) noexcept { p_ += n * static_cast<difference_type>( stride_ ); return *this; }
        constexpr strided_view_iterator & operator-=( difference_type const n ) noexcept { p_ -= n * static_cast<difference_type>( stride_ ); return *this; }

        [[ nodiscard ]] friend constexpr strided_view_iterator operator+( strided_view_iterator it, difference_type const n ) noexcept { return it += n; }
        [[ nodiscard ]] friend constexpr strided_view_iterator operator+( difference_type const n, strided_view_iterator it ) noexcept { return it += n; }
        [[ nodiscard ]] friend constexpr strided_view_iterator operator-( strided_view_iterator it, difference_type const n ) noexcept { return it -= n; }

        [[ nodiscard ]] friend constexpr difference_type operator-( strided_view_iterator const a, strided_view_iterator const b ) noexcept
        {
            BOOST_ASSERT( a.stride_ == b.stride_ );
            BOOST_ASSERT( a.stride_ >= 1 );
            return ( a.p_ - b.p_ ) / static_cast<difference_type>( a.stride_ );
        }

        [[ nodiscard ]] friend constexpr bool operator== ( strided_view_iterator const a, strided_view_iterator const b ) noexcept { return a.p_ ==  b.p_; }
        [[ nodiscard ]] friend constexpr auto operator<=>( strided_view_iterator const a, strided_view_iterator const b ) noexcept { return a.p_ <=> b.p_; }

        /// Cross-type comparison against the const/mutable counterpart
        template <typename U>
        requires( std::is_same_v<std::remove_const_t<T>, std::remove_const_t<U>> && !std::is_same_v<T, U> )
        [[ nodiscard ]] friend constexpr bool operator== ( strided_view_iterator const a, strided_view_iterator<U> const b ) noexcept { return a.base() ==  b.base(); }
        template <typename U>
        requires( std::is_same_v<std::remove_const_t<T>, std::remove_const_t<U>> && !std::is_same_v<T, U> )
        [[ nodiscard ]] friend constexpr auto operator<=>( strided_view_iterator const a, strided_view_iterator<U> const b ) noexcept { return a.base() <=> b.base(); }

        /// Non-standard accessors, for the conversion and comparisons above
        [[ nodiscard ]] constexpr byte_ptr    base  () const noexcept { return p_;      }
        [[ nodiscard ]] constexpr stride_type stride() const noexcept { return stride_; }

    private:
        byte_ptr    p_     {};
        stride_type stride_{ sizeof( T ) };
    }; // class strided_view_iterator
} // namespace detail

////////////////////////////////////////////////////////////////////////////////
/// \class strided_view
////////////////////////////////////////////////////////////////////////////////

template <typename T>
class [[ nodiscard ]] strided_view
{
public:
    using element_type    = T;
    using value_type      = std::remove_cv_t<T>;
    /// Sized like the entry counts strided_vector addresses, which keeps the
    /// whole view in two machine words (generic code caches one per field).
    using size_type       = std::uint32_t;
    using stride_type     = std::uint32_t;
    using difference_type = std::ptrdiff_t;
    using byte_ptr        = detail::strided_view_byte_ptr<T>;
    using       iterator  = detail::strided_view_iterator<T>;
    using const_iterator  = detail::strided_view_iterator<T const>;

    constexpr strided_view() noexcept = default;

    /// \param base        the field within the first entry
    /// \param strideBytes the distance between successive entries
    /// \param size        the number of entries
    constexpr strided_view( T * const base, std::size_t const strideBytes, size_type const size ) noexcept
        : base_{ reinterpret_cast<byte_ptr>( base ) }, stride_{ static_cast<stride_type>( strideBytes ) }, size_{ size }
    {
        BOOST_ASSERT( strideBytes >= sizeof( T ) );
        BOOST_ASSERT( strideBytes <= std::numeric_limits<stride_type>::max() );
    }

    /// The field at \p byteOffset into entries \p strideBytes apart, counting
    /// from the start of the first entry: the shape of one column of an
    /// interleaved record array.
    constexpr strided_view( byte_ptr const entries, std::size_t const byteOffset, std::size_t const strideBytes, size_type const size ) noexcept
        : strided_view{ reinterpret_cast<T *>( entries + byteOffset ), strideBytes, size }
    {
        BOOST_ASSERT( byteOffset + sizeof( T ) <= strideBytes );
    }

    /// Conversion from strided_view<T> to strided_view<T const>
    template <typename U>
    requires( std::is_same_v<T, U const> )
    constexpr strided_view( strided_view<U> const other ) noexcept
        : base_{ other.base() }, stride_{ other.strideBytes() }, size_{ other.size() } {}

    [[ nodiscard, gnu::pure ]] constexpr T & operator[]( size_type const i ) const noexcept
    {
        BOOST_ASSERT( i < size_ );
        return *reinterpret_cast<T *>( base_ + std::size_t{ i } * stride_ );
    }

    [[ nodiscard, gnu::pure ]] constexpr size_type   size       () const noexcept { return size_; }
    [[ nodiscard, gnu::pure ]] constexpr bool        empty      () const noexcept { return size_ == 0; }
    [[ nodiscard, gnu::pure ]] constexpr stride_type strideBytes() const noexcept { return stride_; }
    /// Whether successive elements are adjacent, i.e. this is a std::span in
    /// disguise (and only then does data() answer).
    [[ nodiscard, gnu::pure ]] constexpr bool        contiguous () const noexcept { return stride_ == sizeof( T ); }
    [[ nodiscard, gnu::pure ]] constexpr T *         data       () const noexcept { BOOST_ASSERT( contiguous() ); return reinterpret_cast<T *>( base_ ); }
    [[ nodiscard, gnu::pure ]] constexpr byte_ptr    base       () const noexcept { return base_; }

    [[ nodiscard, gnu::pure ]] constexpr iterator begin() const noexcept { return { base_, stride_ }; }
    [[ nodiscard, gnu::pure ]] constexpr iterator end  () const noexcept { return { base_ + std::size_t{ size_ } * stride_, stride_ }; }

    [[ nodiscard, gnu::pure ]] constexpr T & front() const noexcept { BOOST_ASSERT( !empty() ); return (*this)[ 0         ]; }
    [[ nodiscard, gnu::pure ]] constexpr T & back () const noexcept { BOOST_ASSERT( !empty() ); return (*this)[ size_ - 1 ]; }

    /// Reinterpret the elements as a same-sized type (an enum, an id wrapper).
    template <typename U>
    requires( sizeof( U ) == sizeof( T ) )
    [[ nodiscard, gnu::pure ]] constexpr strided_view<U> as() const noexcept { return { reinterpret_cast<U *>( base_ ), stride_, size_ }; }

    [[ nodiscard ]] constexpr bool operator==( strided_view const & ) const noexcept = default;

private:
    byte_ptr    base_  {};
    stride_type stride_{ sizeof( T ) };
    size_type   size_  {};
}; // class strided_view

static_assert( sizeof( strided_view<std::uint32_t const> ) == 2 * sizeof( void * ) );
static_assert( std::random_access_iterator<strided_view<std::uint32_t const>::iterator> );

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
/// A view that owns nothing: its iterators outlive the view object itself.
template <typename T> constexpr bool std::ranges::enable_borrowed_range<psi::vm::strided_view<T>>{ true };
template <typename T> constexpr bool std::ranges::enable_view          <psi::vm::strided_view<T>>{ true };
//------------------------------------------------------------------------------
