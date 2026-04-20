////////////////////////////////////////////////////////////////////////////////
///
/// \file strided_vector.hpp
/// ------------------------
///
/// Strided vector container: a vector whose "entry" is a runtime-fixed-length
/// contiguous block (stride) of `T`. Entries are stored flat in a single
/// contiguous buffer (a `psi::vm::vector<Storage, Growth>`). Dereference
/// yields a proxy reference (strided_reference) with deep-copy assignment
/// semantics, enabling generic algorithms (sort, rotate, shuffle, …) to
/// permute entries in place.
///
/// It is essentially `std::vector<std::array<T, N>>` where `N` is a
/// runtime-chosen constant bounded at compile time by `MaxStride`. The public
/// API mirrors std::vector as closely as the proxy-reference shape allows
/// (operator[] / at / front / back / begin / end / push_back / emplace_back /
/// pop_back / insert / erase / resize / reserve / capacity / shrink_to_fit /
/// clear / swap / comparisons). The iterator satisfies
/// `std::random_access_iterator` and `std::sortable`.
///
/// Template parameters:
///   - T          : scalar element type carried inside each entry
///   - MaxStride  : compile-time upper bound on the runtime stride (default 64).
///                  Determines both the stride integer type (smallest unsigned
///                  that fits MaxStride, via boost::uint_value_t) and the owning
///                  value_type storage strategy (fc_vector when the worst-case
///                  entry fits in 256 bytes, small_vector with 128-byte SBO
///                  otherwise).
///   - Storage    : VectorStorage contract implementation for the backing buffer
///                  (default: heap_storage<T>)
///   - Growth     : geometric-growth policy for the backing buffer (default: 1.5x)
///
/// Prior art:
///   - std::mdarray (P1684): container-as-template, stride as construction-time
///     invariant — same design choices.
///   - Apache Arrow FixedSizeList: flat-buffer + stride data model.
///   - flecs / entt ECS archetypes: the only mutable strided prior art, but
///     type-erased bytes; strided_vector fills the typed-owning-growable gap.
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

#include <psi/vm/containers/fc_vector.hpp>
#include <psi/vm/containers/growth_policy.hpp>
#include <psi/vm/containers/heap_vector.hpp>
#include <psi/vm/containers/small_vector.hpp>
#include <psi/vm/containers/storage/heap.hpp>
#include <psi/vm/containers/vector.hpp>

#include <boost/assert.hpp>
#include <boost/integer.hpp>

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace detail
{
    // pure wrapper to silence warnings in assume expressions
    [[ gnu::pure ]] constexpr auto sz( std::ranges::range auto const & r ) noexcept { return std::ranges::size( r ); }

    //--------------------------------------------------------------------------
    // stride_uint_for<MaxStride> — smallest unsigned int that can hold MaxStride
    //--------------------------------------------------------------------------
    template <unsigned MaxStride>
    using stride_uint_for = typename boost::uint_value_t<MaxStride>::least;

    // [[gnu::pure]] wrapper for .size() on external types (std::span, std::vector,
    // etc.) so BOOST_ASSUME expressions don't trigger "expression with side effects
    // used in an unevaluated context" warnings on Clang.
    template <typename R>
    [[ nodiscard, gnu::pure ]] constexpr auto sz( R const & r ) noexcept { return r.size(); }

    // Forward declarations for cross-type comparisons
    template <typename T, unsigned MaxStride> class strided_reference;

    //==========================================================================
    // strided_value — owning "entry" type used as iter_value_t
    //==========================================================================
    //
    // Owns `stride` `T`s that represent a single logical entry. Used as
    // iter_value_t: algorithms that need a temporary (sort's pivot,
    // rotate's displaced element) materialize one via iter_move and write it
    // back via strided_reference's deep-copy assignment.
    //
    // Storage strategy (compile-time selection based on MaxStride):
    //
    //   If the worst-case entry (MaxStride * sizeof(T)) fits in 256 bytes,
    //   the entire entry lives in a fixed-capacity inline buffer (fc_vector)
    //   — zero heap allocations, ever.
    //
    //   Otherwise, a small_vector with a 128-byte inline buffer is used:
    //   entries up to 128/sizeof(T) elements stay on the stack; larger ones
    //   spill to the heap. This covers the common case (stride ≤ 20, 4-byte
    //   elements → 80 bytes → fits the SBO buffer).
    //
    //--------------------------------------------------------------------------
    template <typename T, unsigned MaxStride>
    class strided_value
    {
        static constexpr unsigned max_entry_bytes{ MaxStride * static_cast<unsigned>( sizeof( T ) ) };
        static constexpr bool     use_fixed      { max_entry_bytes <= 256  };

        // 128-byte SBO: how many T's fit in the inline buffer.
        // At least 1 so the small_vector always has *some* inline capacity.
        static constexpr std::uint32_t sbo_elems{ std::max<std::uint32_t>( 1u, 128u / static_cast<std::uint32_t>( sizeof( T ) ) ) };

        using storage_type = std::conditional_t<
            use_fixed,
            fc_vector<T, static_cast<std::uint32_t>( MaxStride )>,
            small_vector<T, sbo_elems>
        >;

    public:
        static constexpr bool nothrow_storage{ use_fixed };

        using value_type = T;
        using size_type  = typename storage_type::size_type;

        constexpr strided_value() noexcept = default;

        explicit constexpr strided_value( size_type const n ) noexcept( nothrow_storage )
            : data_( static_cast<typename storage_type::size_type>( n ) ) {}

        explicit constexpr strided_value( std::span<T const> const src ) noexcept( nothrow_storage && std::is_nothrow_copy_constructible_v<T> )
            : data_( src.begin(), src.end() ) {}

        //-- span-like interface --------------------------------------------------
        [[ nodiscard ]] constexpr std::span<T const> as_span() const noexcept { return { data_.data(), data_.size() }; }
        [[ nodiscard ]] constexpr std::span<T      > as_span()       noexcept { return { data_.data(), data_.size() }; }

        [[ nodiscard, gnu::pure ]] constexpr T const * data () const noexcept { return data_.data();  }
        [[ nodiscard, gnu::pure ]] constexpr T       * data ()       noexcept { return data_.data();  }
        [[ nodiscard, gnu::pure ]] constexpr size_type size () const noexcept { return data_.size();  }
        [[ nodiscard, gnu::pure ]] constexpr bool      empty() const noexcept { return data_.empty(); }

        [[ nodiscard ]] constexpr T const & operator[]( size_type const i ) const noexcept { return data_[ i ]; }
        [[ nodiscard ]] constexpr T       & operator[]( size_type const i )       noexcept { return data_[ i ]; }

        [[ nodiscard, gnu::pure ]] constexpr T const * begin() const noexcept { return data();                }
        [[ nodiscard, gnu::pure ]] constexpr T       * begin()       noexcept { return data();                }
        [[ nodiscard, gnu::pure ]] constexpr T const * end  () const noexcept { return data() + data_.size(); }
        [[ nodiscard, gnu::pure ]] constexpr T       * end  ()       noexcept { return data() + data_.size(); }

        //-- comparisons (lexicographic over the entry's Ts) ---------------------
        [[ nodiscard ]] friend constexpr bool operator==( strided_value const & a, strided_value const & b ) noexcept {
            BOOST_ASSUME( a.size() == b.size() );
            return std::ranges::equal( a.as_span(), b.as_span() );
        }
        [[ nodiscard ]] friend constexpr auto operator<=>( strided_value const & a, strided_value const & b ) noexcept {
            BOOST_ASSUME( a.size() == b.size() );
            return std::lexicographical_compare_three_way( a.begin(), a.end(), b.begin(), b.end() );
        }

        // Cross-type: strided_value vs strided_reference (needed by std::sort's
        // pivot comparisons where the value_type temporary is on the LHS).
        // Explicit operator< provided alongside <=> because MSVC STL's
        // std::less<void> SFINAE check does not consistently synthesize
        // operator< from operator<=> for hidden friends in all contexts.
        template <typename U, unsigned MS2>
        [[ nodiscard ]] friend constexpr bool operator==( strided_value const & a, strided_reference<U, MS2> const b ) noexcept {
            BOOST_ASSUME( a.size() == b.size() );
            return std::ranges::equal( a.as_span(), b );
        }
        template <typename U, unsigned MS2>
        [[ nodiscard ]] friend constexpr auto operator<=>( strided_value const & a, strided_reference<U, MS2> const b ) noexcept {
            BOOST_ASSUME( a.size() == b.size() );
            return std::lexicographical_compare_three_way( a.begin(), a.end(), b.begin(), b.end() );
        }

        // Explicit relational operators for all type combinations — needed for
        // std::totally_ordered_with (required by std::ranges::less and
        // indirect_strict_weak_order). Spaceship rewrite doesn't always work
        // through SFINAE in std::ranges CPOs on MSVC STL.
        template <typename U, unsigned MS2>
        [[ nodiscard ]] friend constexpr bool operator< ( strided_value const & a, strided_reference<U, MS2> const b ) noexcept { return ( a <=> b ) <  0; }
        template <typename U, unsigned MS2>
        [[ nodiscard ]] friend constexpr bool operator> ( strided_value const & a, strided_reference<U, MS2> const b ) noexcept { return ( a <=> b ) >  0; }
        template <typename U, unsigned MS2>
        [[ nodiscard ]] friend constexpr bool operator<=( strided_value const & a, strided_reference<U, MS2> const b ) noexcept { return ( a <=> b ) <= 0; }
        template <typename U, unsigned MS2>
        [[ nodiscard ]] friend constexpr bool operator>=( strided_value const & a, strided_reference<U, MS2> const b ) noexcept { return ( a <=> b ) >= 0; }

        // value vs value
        [[ nodiscard ]] friend constexpr bool operator< ( strided_value const & a, strided_value const & b ) noexcept { return ( a <=> b ) <  0; }
        [[ nodiscard ]] friend constexpr bool operator> ( strided_value const & a, strided_value const & b ) noexcept { return ( a <=> b ) >  0; }
        [[ nodiscard ]] friend constexpr bool operator<=( strided_value const & a, strided_value const & b ) noexcept { return ( a <=> b ) <= 0; }
        [[ nodiscard ]] friend constexpr bool operator>=( strided_value const & a, strided_value const & b ) noexcept { return ( a <=> b ) >= 0; }

    private:
        storage_type data_;
    }; // class strided_value


    //==========================================================================
    // strided_reference — proxy reference used as iter_reference_t
    //==========================================================================
    //
    // Holds a (pointer, stride) pair and writes *through* the pointer on
    // assignment — the same recipe std::vector<bool>::reference follows.
    // This is what lets generic algorithms (e.g. std::sort, std::ranges::sort,
    // std::rotate, std::shuffle) move and swap entries in place.
    //
    // Key contract:
    //   operator=(span)    — deep-copies `stride` Ts into the referenced slot.
    //   operator=(reference) — deep-copies from another proxy (handles self).
    //   operator=(value &&)  — move-assigns entry contents back into the slot.
    //
    // All assignment overloads are `const`-qualified so that assignment to a
    // `const iter_reference_t&&` (required by std::indirectly_writable) still
    // compiles.
    //--------------------------------------------------------------------------
    template <typename T, unsigned MaxStride>
    class strided_reference
    {
    public:
        using element_type = T;
        using stride_type  = stride_uint_for<MaxStride>;
        using value_type   = strided_value<std::remove_const_t<T>, MaxStride>;

        constexpr strided_reference() noexcept = default;
        constexpr strided_reference( T * const ptr, stride_type const stride ) noexcept
            : ptr_{ ptr }, stride_{ stride } {}

        // Copy ctor aliases the same underlying slot (std::vector<bool>-style).
        strided_reference( strided_reference const & ) noexcept = default;

        //-- deep-copy assignment ------------------------------------------------
        constexpr strided_reference const & operator=( std::span<std::remove_const_t<T> const> const src ) const noexcept( std::is_nothrow_copy_assignable_v<T> )
        requires( !std::is_const_v<T> )
        {
            BOOST_ASSUME( stride_ >= 1 );
            BOOST_ASSERT( src.size() == stride_ );
            std::ranges::copy( src, ptr_ );
            return *this;
        }

        constexpr strided_reference const & operator=( strided_reference const other ) const noexcept( std::is_nothrow_copy_assignable_v<T> )
        requires( !std::is_const_v<T> )
        {
            BOOST_ASSUME( stride_ >= 1 );
            if ( ptr_ != other.ptr_ ) {
                std::ranges::copy_n( other.ptr_, stride_, ptr_ );
            }
            return *this;
        }

        constexpr strided_reference const & operator=( value_type const & v ) const noexcept( std::is_nothrow_copy_assignable_v<T> )
        requires( !std::is_const_v<T> )
        {
            BOOST_ASSUME( stride_ >= 1 );
            BOOST_ASSERT( v.size() == stride_ );
            std::ranges::copy( v.as_span(), ptr_ );
            return *this;
        }

        constexpr strided_reference const & operator=( value_type && v ) const noexcept( std::is_nothrow_move_assignable_v<T> )
        requires( !std::is_const_v<T> )
        {
            BOOST_ASSUME( stride_ >= 1 );
            BOOST_ASSERT( v.size() == stride_ );
            std::ranges::move( v.as_span(), ptr_ );
            return *this;
        }

        //-- implicit conversions ------------------------------------------------
        constexpr operator std::span<T>() const noexcept { BOOST_ASSUME( stride_ >= 1 ); return { ptr_, stride_ }; }

        // Materialize the entry as an owning value (deep copy).
        constexpr operator value_type() const noexcept( value_type::nothrow_storage && std::is_nothrow_copy_constructible_v<std::remove_const_t<T>> ) {
            BOOST_ASSUME( stride_ >= 1 );
            return value_type{ std::span<T const>{ ptr_, stride_ } };
        }

        //-- span-like read interface --------------------------------------------
        [[ nodiscard ]] static     constexpr bool        empty()       noexcept { return false;   }
        [[ nodiscard, gnu::pure ]] constexpr T *         data () const noexcept { return ptr_;    }
        [[ nodiscard, gnu::pure ]] constexpr T *         begin() const noexcept { return ptr_;    }
        [[ nodiscard, gnu::pure ]] constexpr T *         end  () const noexcept { return begin() + size(); }
        [[ nodiscard, gnu::pure ]] constexpr stride_type size () const noexcept { BOOST_ASSUME( stride_ >= 1 );
                                                                                   return stride_; }
        [[ nodiscard, gnu::pure ]] constexpr T & operator[]( stride_type const i ) const noexcept { return ptr_[ i ]; }

        //-- in-place element swap (ADL-visible — picked up by std::ranges::iter_swap)
        friend constexpr void swap( strided_reference const a, strided_reference const b ) noexcept( std::is_nothrow_swappable_v<T> )
        requires( !std::is_const_v<T> )
        {
            BOOST_ASSUME( a.stride_ >= 1 );
            BOOST_ASSERT( a.stride_ == b.stride_ );
            if ( a.ptr_ == b.ptr_ ) [[ unlikely ]] {
                return;
            }
            for ( auto i{ 0U }; i < a.stride_; ++i )
            {
                using std::swap;
                swap( a.ptr_[ i ], b.ptr_[ i ] );
            }
        }

        //-- comparisons (element-wise, lexicographic) --------------------------
        // Non-template same-type ==/<=> (required by std::totally_ordered — the
        // concept check does not reliably find template hidden friends via SFINAE).
        [[ nodiscard ]] friend constexpr bool operator== ( strided_reference const a, strided_reference const b ) noexcept { BOOST_ASSUME( a.size() == b.size() ); return std::ranges::equal( a, b ); }
        [[ nodiscard ]] friend constexpr auto operator<=>( strided_reference const a, strided_reference const b ) noexcept { BOOST_ASSUME( a.size() == b.size() ); return std::lexicographical_compare_three_way( a.begin(), a.end(), b.begin(), b.end() ); }

        //-- comparisons against owning value ------------------------------------
        [[ nodiscard ]] friend constexpr bool operator== ( strided_reference const a, value_type const & b ) noexcept { BOOST_ASSUME( a.size() == b.size() ); return std::ranges::equal( a, b.as_span() ); }
        [[ nodiscard ]] friend constexpr auto operator<=>( strided_reference const a, value_type const & b ) noexcept { BOOST_ASSUME( a.size() == b.size() ); return std::lexicographical_compare_three_way( a.begin(), a.end(), b.begin(), b.end() ); }
        // Explicit relational operators for all type combinations (see strided_value for rationale)
        [[ nodiscard ]] friend constexpr bool operator< ( strided_reference const a, value_type const & b ) noexcept { return ( a <=> b ) <  0; }
        [[ nodiscard ]] friend constexpr bool operator> ( strided_reference const a, value_type const & b ) noexcept { return ( a <=> b ) >  0; }
        [[ nodiscard ]] friend constexpr bool operator<=( strided_reference const a, value_type const & b ) noexcept { return ( a <=> b ) <= 0; }
        [[ nodiscard ]] friend constexpr bool operator>=( strided_reference const a, value_type const & b ) noexcept { return ( a <=> b ) >= 0; }

    private:
        T *         ptr_   { nullptr };
        stride_type stride_{ 1       };
    }; // class strided_reference


    //==========================================================================
    // strided_iterator — random-access iterator yielding strided_reference
    //==========================================================================
    //
    // The iterator is a thin (pointer, stride) pair; operator++ advances by
    // `stride` Ts. Dereference returns a strided_reference proxy.
    //
    // Customization points provided via hidden friends so that
    // std::ranges::iter_swap / std::ranges::iter_move pick them up via ADL:
    //   - iter_swap: element-wise in-place exchange
    //   - iter_move: materialize a strided_value by moving the slot's contents
    //
    // This makes the iterator a model of std::random_access_iterator AND
    // satisfies std::sortable, so std::sort / std::ranges::sort / rotate /
    // reverse / etc. all work.
    //--------------------------------------------------------------------------
    template <typename T, unsigned MaxStride>
    class strided_iterator
    {
    public:
        using stride_type       = stride_uint_for<MaxStride>;
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept  = std::random_access_iterator_tag;
        using value_type        = strided_value    <std::remove_const_t<T>, MaxStride>;
        using reference         = strided_reference<T, MaxStride>;
        using difference_type   = std::ptrdiff_t;
        using pointer           = void; // no meaningful arrow — reference is a proxy

        constexpr strided_iterator() noexcept = default;
        constexpr strided_iterator( T * const ptr, stride_type const stride ) noexcept
            : ptr_{ ptr }, stride_{ stride } {}

        // Conversion from iterator<T> to iterator<T const>
        template <typename U>
        requires( std::is_same_v<T, U const> )
        constexpr strided_iterator( strided_iterator<U, MaxStride> const other ) noexcept
            : ptr_{ other.base() }, stride_{ other.stride() } {}

        [[ nodiscard ]] constexpr reference operator* () const noexcept { BOOST_ASSUME( stride_ >= 1 ); return { ptr_, stride_ }; }
        [[ nodiscard ]] constexpr reference operator[]( difference_type const n ) const noexcept { BOOST_ASSUME( stride_ >= 1 ); return { ptr_ + n * stride_, stride_ }; }

        constexpr strided_iterator & operator++(   ) noexcept { BOOST_ASSUME( stride_ >= 1 ); ptr_ += stride_; return *this; }
        constexpr strided_iterator   operator++(int) noexcept { BOOST_ASSUME( stride_ >= 1 ); auto tmp{ *this }; ++*this; return tmp; }
        constexpr strided_iterator & operator--(   ) noexcept { BOOST_ASSUME( stride_ >= 1 ); ptr_ -= stride_; return *this; }
        constexpr strided_iterator   operator--(int) noexcept { BOOST_ASSUME( stride_ >= 1 ); auto tmp{ *this }; --*this; return tmp; }

        constexpr strided_iterator & operator+=( difference_type const n ) noexcept { BOOST_ASSUME( stride_ >= 1 ); ptr_ += n * stride_; return *this; }
        constexpr strided_iterator & operator-=( difference_type const n ) noexcept { BOOST_ASSUME( stride_ >= 1 ); ptr_ -= n * stride_; return *this; }

        [[ nodiscard ]] friend constexpr strided_iterator operator+( strided_iterator it, difference_type const n ) noexcept { BOOST_ASSUME( it.stride_ >= 1 ); return it += n; }
        [[ nodiscard ]] friend constexpr strided_iterator operator+( difference_type const n, strided_iterator it ) noexcept { BOOST_ASSUME( it.stride_ >= 1 ); return it += n; }
        [[ nodiscard ]] friend constexpr strided_iterator operator-( strided_iterator it, difference_type const n ) noexcept { BOOST_ASSUME( it.stride_ >= 1 ); return it -= n; }

        [[ nodiscard ]] friend constexpr difference_type operator-( strided_iterator const a, strided_iterator const b ) noexcept
        {
            BOOST_ASSERT( a.stride_ == b.stride_ );
            BOOST_ASSUME( a.stride_ >= 1 );
            return ( a.ptr_ - b.ptr_ ) / a.stride_;
        }

        [[ nodiscard ]] friend constexpr bool operator== ( strided_iterator const a, strided_iterator const b ) noexcept { return a.ptr_ ==  b.ptr_; }
        [[ nodiscard ]] friend constexpr auto operator<=>( strided_iterator const a, strided_iterator const b ) noexcept { return a.ptr_ <=> b.ptr_; }

        //-- customization points for std::ranges --------------------------------
        friend constexpr value_type iter_move( strided_iterator const it )
        noexcept( value_type::nothrow_storage && std::is_nothrow_move_constructible_v<std::remove_const_t<T>> )
        {
            BOOST_ASSUME( it.stride_ >= 1 );
            value_type v( it.stride_ );
            std::ranges::move( std::span<T>{ it.ptr_, it.stride_ }, v.data() );
            return v;
        }

        friend constexpr void iter_swap( strided_iterator const a, strided_iterator const b )
        noexcept( std::is_nothrow_swappable_v<std::remove_const_t<T>> )
        requires( !std::is_const_v<T> )
        {
            BOOST_ASSUME( a.stride_ >= 1 );
            BOOST_ASSERT( a.stride_ == b.stride_ );
            if ( a.ptr_ == b.ptr_ ) {
                return;
            }
            for ( stride_type i{ 0 }; i < a.stride_; ++i )
            {
                using std::swap;
                swap( a.ptr_[ i ], b.ptr_[ i ] );
            }
        }

        // Non-standard accessors for cross-type comparison + const-conversion
        [[ nodiscard ]] constexpr T *         base  () const noexcept { return ptr_;    }
        [[ nodiscard ]] constexpr stride_type stride() const noexcept { return stride_; }

    private:
        T *         ptr_   { nullptr };
        stride_type stride_{ 1       };
    }; // class strided_iterator
} // namespace detail

////////////////////////////////////////////////////////////////////////////////
/// \class strided_vector
///
/// \brief Vector of runtime-fixed-extent `T` entries, bounded at compile time
///        by `MaxStride`.
///
/// Each entry occupies `stride()` consecutive `T`s in a flat backing buffer
/// (`vector<Storage, Growth>`). All std::vector-like size/capacity/mutation
/// operations count in entries, not in underlying `T`s.
///
/// `MaxStride` (default 64) caps the runtime stride at compile time. From it
/// the library derives:
///   - `stride_type` — the smallest unsigned integer that fits MaxStride
///     (via `boost::uint_value_t<MaxStride>::least`)
///   - `value_type` storage — `fc_vector` when the worst-case entry fits in
///     256 bytes, `small_vector` with 128-byte SBO otherwise
///
/// The actual stride is set at construction (or via `init(stride)` on a
/// default-constructed instance) and must not change while the container
/// holds entries. `clear()` does not reset stride.
////////////////////////////////////////////////////////////////////////////////

template <typename T,
          unsigned MaxStride      = 64,
          typename Storage        = heap_storage<T>,
          geometric_growth Growth = geometric_growth{}>
class [[ nodiscard ]] strided_vector
{
public:
    using backing_vector_type = vector<Storage, Growth>;

    // tag so free erase/erase_if-style helpers can be constrained
    using psi_vm_strided_vector_tag = void;

    // Mirrors std::vector<bool>: each "entry" is the logical `value_type`,
    // and the underlying scalar is exposed as `element_type`.
    using element_type  = T;
    using value_type    = detail::strided_value<T, MaxStride>;
    using stride_type   = detail::stride_uint_for<MaxStride>;
    using storage_type  = Storage;
    static constexpr unsigned max_stride = MaxStride;

    using size_type       = typename backing_vector_type::size_type;
    using difference_type = std::make_signed_t<size_type>;

    using       pointer   = element_type       *;
    using const_pointer   = element_type const *;

    // Proxy reference: writes through the flat buffer on assignment, enabling
    // generic algorithms (std::sort, std::ranges::sort, std::rotate, …) to
    // permute entries in place. See detail::strided_reference.
    using       reference = detail::strided_reference<T      , MaxStride>;
    using const_reference = detail::strided_reference<T const, MaxStride>;

    using       iterator         = detail::strided_iterator<T      , MaxStride>;
    using const_iterator         = detail::strided_iterator<T const, MaxStride>;
    using       reverse_iterator = std::reverse_iterator<      iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    //--------------------------------------------------------------------------
    // Construction
    //--------------------------------------------------------------------------
    constexpr strided_vector() noexcept = default;

    constexpr strided_vector            ( strided_vector const & ) = default;
    constexpr strided_vector & operator=( strided_vector const & ) = default;

    constexpr strided_vector( strided_vector && other ) noexcept( std::is_nothrow_move_constructible_v<backing_vector_type> )
        : data_  { std::move( other.data_ ) }
        , stride_{ std::exchange( other.stride_, stride_type{ 0 } ) }
    {}

    constexpr strided_vector & operator=( strided_vector && other ) noexcept( std::is_nothrow_move_assignable_v<backing_vector_type> )
    {
        if ( this != &other ) [[ likely ]] {
            data_   = std::move    ( other.data_ );
            stride_ = std::exchange( other.stride_, stride_type{ 0 } );
        }
        return *this;
    }

    // Construct empty with stride preset
    explicit constexpr strided_vector( stride_type const stride ) noexcept : stride_{ stride } { BOOST_ASSUME( stride <= MaxStride ); }

    // Construct `count` default-initialized entries of the given stride
    constexpr strided_vector( stride_type const stride, size_type const count )
        : data_( static_cast<size_type>( count ) * stride ), stride_{ stride }
    { BOOST_ASSUME( stride <= MaxStride ); }

    // Construct `count` entries initialized from a prototype span
    constexpr strided_vector( stride_type const stride, size_type const count, std::span<T const> const prototype )
        : stride_{ stride }
    {
        BOOST_ASSUME( stride <= MaxStride );
        BOOST_ASSERT( prototype.size() == stride );
        data_.reserve( count * stride );
        for ( size_type i{ 0 }; i < count; ++i ) {
            data_.append_range( prototype );
        }
    }

    // Construct from a range of spans (each of length stride)
    template <std::ranges::input_range EntryRng>
    requires std::convertible_to<std::ranges::range_reference_t<EntryRng>, std::span<T const>>
    constexpr strided_vector( stride_type const stride, EntryRng && entries ) : stride_{ stride }
    {
        BOOST_ASSUME( stride <= MaxStride );
        if constexpr ( std::ranges::sized_range<EntryRng> ) {
            data_.reserve( static_cast<size_type>( std::ranges::size( entries ) ) * stride );
        }
        for ( std::span const e : entries ) {
            push_back( e );
        }
    }

    //--------------------------------------------------------------------------
    // Stride control
    //--------------------------------------------------------------------------

    /// Clears the container and sets a new stride.
    void init( stride_type const s ) noexcept
    {
        BOOST_ASSUME( s <= MaxStride );
        stride_ = s;
        clear();
    }

    [[ nodiscard, gnu::pure ]] constexpr stride_type stride() const noexcept { BOOST_ASSUME( stride_ <= MaxStride ); return stride_; }

    //--------------------------------------------------------------------------
    // Storage extraction / adoption
    //
    // Allow the backing buffer to be moved out for reuse (cursor walks,
    // external sorting, etc.) and moved back in later.
    //--------------------------------------------------------------------------

    /// Move the backing buffer out, leaving this container empty (stride unchanged).
    [[ nodiscard ]] backing_vector_type extract_data() noexcept { stride_ = 0; return std::move( data_ ); }

    /// Adopt a pre-populated backing buffer. The buffer's element count must
    /// be a multiple of `stride` (asserted). The container takes ownership of
    /// the buffer's contents — nothing is cleared.
    void adopt_data( backing_vector_type && v, stride_type const stride ) noexcept
    {
        BOOST_ASSUME( stride <= MaxStride );
        BOOST_ASSERT( v.empty() || v.size() % stride == 0 );
        stride_ = stride;
        data_   = std::move( v );
    }

    //--------------------------------------------------------------------------
    // Capacity
    //--------------------------------------------------------------------------
    // Minimal stride-0 support: empty/size/capacity/stride are well-defined
    // in the default-constructed / freshly init(0)'d state (used by callers
    // that hold a zero-dim coordinate hash table — see RHSSubspaceData's
    // fully-aggregated-source path). All other methods assume stride_ >= 1
    // and will assert — zero-stride mutation is not supported. The `max`
    // clamp yields a branchless size/capacity computation that divides by
    // 1 when stride_ is 0 (data_ is always empty in that state, so the
    // result is 0 either way — just avoids the explicit zero-check).
    [[ nodiscard, gnu::pure ]]        constexpr bool      empty   () const noexcept { return data_.empty(); }
    [[ nodiscard, gnu::pure ]]        constexpr size_type size    () const noexcept { return static_cast<size_type>( data_.size    () / std::max<stride_type>( stride_, 1 ) ); }
    [[ nodiscard, gnu::pure ]]        constexpr size_type capacity() const noexcept { return static_cast<size_type>( data_.capacity() / std::max<stride_type>( stride_, 1 ) ); }
    [[ nodiscard, gnu::pure ]] static constexpr size_type max_size()       noexcept { return backing_vector_type::max_size(); }

    constexpr void reserve      ( size_type const numEntries )          { data_.reserve( numEntries * stride() ); }
    constexpr void shrink_to_fit(                            ) noexcept { data_.shrink_to_fit(); }

    //--------------------------------------------------------------------------
    // Element access
    //
    // Each entry is addressed by index and exposed as a proxy reference that
    // writes through the underlying buffer on assignment (see strided_reference).
    //--------------------------------------------------------------------------
    [[ nodiscard ]] constexpr reference operator[]( size_type const i ) noexcept
    {
        BOOST_ASSUME( stride_ >= 1 );
        return { data_.data() + i * stride_, stride_ };
    }

    [[ nodiscard ]] constexpr const_reference operator[]( size_type const i ) const noexcept
    {
        BOOST_ASSUME( stride_ >= 1 );
        return { data_.data() + i * stride_, stride_ };
    }

    [[ nodiscard ]] constexpr reference at( size_type const i )
    {
        if ( i >= size() )
            throw std::out_of_range{ "psi::vm::strided_vector::at" };
        return ( *this )[ i ];
    }

    [[ nodiscard ]] constexpr const_reference at( size_type const i ) const
    {
        if ( i >= size() )
            throw std::out_of_range{ "psi::vm::strided_vector::at" };
        return ( *this )[ i ];
    }

    [[ nodiscard ]] constexpr reference       front()       noexcept { BOOST_ASSERT( !empty() ); return (*this)[ 0 ]; }
    [[ nodiscard ]] constexpr const_reference front() const noexcept { BOOST_ASSERT( !empty() ); return (*this)[ 0 ]; }
    [[ nodiscard ]] constexpr reference       back ()       noexcept { BOOST_ASSERT( !empty() ); return (*this)[ size() - 1 ]; }
    [[ nodiscard ]] constexpr const_reference back () const noexcept { BOOST_ASSERT( !empty() ); return (*this)[ size() - 1 ]; }

    //--------------------------------------------------------------------------
    // Raw data access
    //--------------------------------------------------------------------------
    [[ nodiscard ]] constexpr       pointer data()       noexcept { return data_.data(); }
    [[ nodiscard ]] constexpr const_pointer data() const noexcept { return data_.data(); }

    [[ nodiscard ]] constexpr backing_vector_type const & backing() const noexcept { return data_; }

    //--------------------------------------------------------------------------
    // Iterators
    //--------------------------------------------------------------------------
    [[ nodiscard ]] constexpr       iterator  begin()       noexcept { return { data_.data()               , stride_ }; }
    [[ nodiscard ]] constexpr const_iterator  begin() const noexcept { return { data_.data()               , stride_ }; }
    [[ nodiscard ]] constexpr       iterator  end  ()       noexcept { return { data_.data() + data_.size(), stride_ }; }
    [[ nodiscard ]] constexpr const_iterator  end  () const noexcept { return { data_.data() + data_.size(), stride_ }; }
    [[ nodiscard ]] constexpr const_iterator cbegin() const noexcept { return begin(); }
    [[ nodiscard ]] constexpr const_iterator cend  () const noexcept { return end  (); }

    [[ nodiscard ]] constexpr       reverse_iterator  rbegin()       noexcept { return       reverse_iterator{ end  () }; }
    [[ nodiscard ]] constexpr const_reverse_iterator  rbegin() const noexcept { return const_reverse_iterator{ end  () }; }
    [[ nodiscard ]] constexpr       reverse_iterator  rend  ()       noexcept { return       reverse_iterator{ begin() }; }
    [[ nodiscard ]] constexpr const_reverse_iterator  rend  () const noexcept { return const_reverse_iterator{ begin() }; }
    [[ nodiscard ]] constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
    [[ nodiscard ]] constexpr const_reverse_iterator crend  () const noexcept { return rend(); }

    //--------------------------------------------------------------------------
    // Assign (std::vector-compatible, stride-aware)
    //
    // Wipe current contents and repopulate. `stride_` is preserved (matches
    // std::vector's invariant of leaving the allocator/config intact). Use
    // `init(new_stride)` before `assign` if a stride change is also needed.
    //--------------------------------------------------------------------------

    /// Assign `count` copies of a prototype entry.
    void assign( size_type const count, std::span<T const> const prototype )
    {
        BOOST_ASSUME( stride_ >= 1 );
        BOOST_ASSERT( prototype.size() == stride_ );
        data_.clear();
        data_.reserve( count * stride_ );
        for ( size_type i{ 0 }; i < count; ++i ) {
            data_.append_range( prototype );
        }
    }

    /// Assign from a range of span-like entries (each of length stride).
    template <std::ranges::input_range EntryRng>
    requires std::convertible_to<std::ranges::range_reference_t<EntryRng>, std::span<T const>>
    void assign( EntryRng && entries )
    {
        BOOST_ASSUME( stride_ >= 1 );
        data_.clear();
        if constexpr ( std::ranges::sized_range<EntryRng> ) {
            data_.reserve( static_cast<size_type>( std::ranges::size( entries ) ) * stride_ );
        }
        for ( std::span const e : entries ) {
            push_back( e );
        }
    }

    //--------------------------------------------------------------------------
    // Modifiers
    //--------------------------------------------------------------------------
    constexpr void clear() noexcept { data_.clear(); }

    /// Append a single entry. The source span must be exactly `stride`
    /// elements long.
    void push_back( std::span<T const> const entry )
    {
        BOOST_ASSUME( stride_ >= 1 );
        BOOST_ASSUME( detail::sz( entry ) == stride_ );
        ensure_capacity();
        data_.append_range( entry );
    }

    /// Append an entry filled with a single value.
    void push_back_fill( T const value )
    {
        BOOST_ASSUME( stride_ >= 1 );
        ensure_capacity();
        data_.resize( data_.size() + stride_, value );
    }

    /// Construct an entry in place from `stride` scalar arguments.
    /// For proper amortized-O(1) growth the arguments are copied in after
    /// growing the backing vector with geometric headroom.
    template <typename... Args>
    requires( sizeof...( Args ) > 0 && ( std::convertible_to<Args, T> && ... ) )
    reference emplace_back( Args &&... args )
    {
        BOOST_ASSUME( stride_ >= 1 );
        BOOST_ASSUME( sizeof...( Args ) == stride_ );
        ensure_capacity();
        T const src[]{ static_cast<T>( std::forward<Args>( args ) )... };
        data_.append_range( std::span<T const>{ src, sizeof...( Args ) } );
        return back();
    }

    /// Remove the last entry (requires !empty()).
    void pop_back() noexcept
    {
        BOOST_ASSUME( stride_ >= 1 );
        BOOST_ASSUME( !empty() );
        data_.shrink_by( stride_ );
    }

    /// Insert a single entry at `pos`. Returns iterator to the inserted entry.
    iterator insert( const_iterator const pos, std::span<T const> const entry )
    {
        BOOST_ASSUME( stride_ >= 1 );
        BOOST_ASSUME( detail::sz( entry ) == stride_ );
        auto const offset  { static_cast<size_type>( pos - cbegin() ) };
        auto const byte_pos{ data_.cbegin() + static_cast<difference_type>( offset ) * stride_ };
        data_.insert( byte_pos, entry.begin(), entry.end() );
        return begin() + static_cast<difference_type>( offset );
    }

    /// Insert `count` copies of a prototype entry at `pos`.
    /// Single shift of the tail, then fills the gap — O(size + count*stride).
    iterator insert( const_iterator const pos, size_type const count, std::span<T const> const prototype )
    {
        BOOST_ASSUME( stride_ >= 1 );
        BOOST_ASSERT( prototype.size() == stride_ );
        auto const offset   { static_cast<size_type>( pos - cbegin() ) };
        auto const scalarPos{ data_.cbegin() + static_cast<difference_type>( offset ) * stride_ };
        auto const totalTs  { static_cast<size_type>( count ) * stride_ };
        // Make room: single shift of the tail (via backing vector's insert of default-init Ts)
        data_.insert( scalarPos, totalTs, T{} );
        // Fill the gap with `count` copies of prototype
        auto * dst{ data_.data() + static_cast<std::size_t>( offset ) * stride_ };
        for ( size_type i{ 0 }; i < count; ++i, dst += stride_ )
            std::ranges::copy( prototype, dst );
        return begin() + static_cast<difference_type>( offset );
    }

    /// Erase a single entry. Returns iterator to the element after the erased.
    iterator erase( const_iterator const pos ) noexcept
    {
        BOOST_ASSUME( stride_ >= 1 );
        BOOST_ASSERT( pos >= cbegin() && pos < cend() );
        auto const offset  { static_cast<size_type>( pos - cbegin() ) };
        auto const byte_pos{ data_.cbegin() + static_cast<difference_type>( offset ) * stride_ };
        data_.erase( byte_pos, byte_pos + stride_ );
        return begin() + static_cast<difference_type>( offset );
    }

    /// Erase a range of entries [first, last).
    iterator erase( const_iterator const first, const_iterator const last ) noexcept
    {
        BOOST_ASSUME( stride_ >= 1 );
        BOOST_ASSERT( first <= last );
        auto const offset_f{ static_cast<size_type>( first - cbegin() ) };
        auto const offset_l{ static_cast<size_type>( last  - cbegin() ) };
        auto const byte_f  { data_.cbegin() + static_cast<difference_type>( offset_f ) * stride_ };
        auto const byte_l  { data_.cbegin() + static_cast<difference_type>( offset_l ) * stride_ };
        data_.erase( byte_f, byte_l );
        return begin() + static_cast<difference_type>( offset_f );
    }

    /// Resize to `count` entries. New entries are default-initialized.
    void resize( size_type const count ) { data_.resize( count * stride_ ); }

    /// Resize to `count` entries. New entries are filled from `prototype`.
    void resize( size_type const count, std::span<T const> const prototype )
    {
        BOOST_ASSUME( stride_ >= 1 );
        BOOST_ASSUME( detail::sz( prototype ) == stride_ );
        auto const old_entries{ size() };
        if ( count <= old_entries )
        {
            data_.resize( count * stride_ );
        }
        else
        {
            data_.reserve( count * stride_ );
            for ( auto i{ old_entries }; i < count; ++i )
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
        auto const c{ std::lexicographical_compare_three_way( a.data_.begin(), a.data_.end(), b.data_.begin(), b.data_.end() ) };
        if ( c != 0 ) {
            return c;
        }
        return a.stride_ <=> b.stride_;
    }

private:
    // The backing vector's append_range / grow_by use exact-fit growth
    // (geometric_growth{1,1}) — no headroom. Without explicit geometric
    // growth here, every push_back would trigger a reallocation. We apply
    // the same Growth policy the backing vector would use for emplace_back.
    void ensure_capacity( size_type const additionalEntries = 1 )
    {
        BOOST_ASSUME( stride_ >= 1 );
        auto const needed{ data_.size() + additionalEntries * stride_ };
        if ( needed > data_.capacity() )
        {
            data_.reserve( Growth( needed, data_.capacity() ) );
        }
    }

    backing_vector_type data_;
    stride_type         stride_{ 1 };
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
                std::ranges::copy( *read, ( *write ).data() );
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

//------------------------------------------------------------------------------
// std::common_reference support — required by std::indirectly_readable so the
// strided_iterator satisfies std::input_iterator and transitively
// std::sortable / std::permutable.
//
// The proxy reference (strided_reference) and the owning value (strided_value)
// share strided_value as their common reference: the proxy has an implicit
// conversion to strided_value (deep copy) and strided_value is a regular type
// that can copy/move itself.
//------------------------------------------------------------------------------
namespace std
{
    template <typename T, unsigned MaxStride, template <typename> class TQual, template <typename> class UQual>
    struct basic_common_reference<
        ::psi::vm::detail::strided_reference<T, MaxStride>,
        ::psi::vm::detail::strided_value    <std::remove_const_t<T>, MaxStride>,
        TQual, UQual>
    {
        using type = ::psi::vm::detail::strided_value<std::remove_const_t<T>, MaxStride>;
    };

    template <typename T, unsigned MaxStride, template <typename> class TQual, template <typename> class UQual>
    struct basic_common_reference<
        ::psi::vm::detail::strided_value    <std::remove_const_t<T>, MaxStride>,
        ::psi::vm::detail::strided_reference<T, MaxStride>,
        TQual, UQual>
    {
        using type = ::psi::vm::detail::strided_value<std::remove_const_t<T>, MaxStride>;
    };
} // namespace std
//------------------------------------------------------------------------------
