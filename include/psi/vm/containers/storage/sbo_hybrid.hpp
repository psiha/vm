////////////////////////////////////////////////////////////////////////////////
///
/// \file sbo_hybrid.hpp
/// --------------------
///
/// Storage backend for small_vector: inline (stack) buffer with heap spill.
/// Provides the VectorStorage contract consumed by vector<sbo_hybrid<T,N>>.
///
/// Three union-based layout modes, selected at compile time via
/// sbo_options.layout:
///   compact     - separate size_ field, MSB = heap flag
///   compact_lsb - size_ field first (LSB = heap flag), size = size_ >> 1
///   embedded    - sz_ packed inside the union (common initial sequence)
///
/// All layouts are trivially relocatable when T is (just memcpy the object).
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

#include <psi/vm/allocators/crt.hpp>
#include <psi/vm/containers/growth_policy.hpp>
#include <psi/vm/containers/is_trivially_moveable.hpp>
#include <psi/vm/containers/trivially_destructible_after_move.hpp>
#include <psi/vm/containers/noninitialized_array.hpp>

#include <psi/build/attributes.hpp>

#include <boost/assert.hpp>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
// Layout enum, options struct, and compile-time layout resolver.
// (Defined here so storage/sbo_hybrid.hpp is self-contained; small_vector.hpp
//  includes this header and re-exports these names into psi::vm.)
////////////////////////////////////////////////////////////////////////////////

enum class sbo_layout : std::uint8_t {
    auto_select,   // default -- resolved to the layout with the smallest footprint
    compact,       // union-based, MSB-of-size flag, trivially relocatable
    compact_lsb,   // union-based, size-first with LSB flag, trivially relocatable
    embedded,      // union-based, size inside union (LSB flag), trivially relocatable
};

struct sbo_options
{
    std::uint8_t     alignment{ 0 };
    geometric_growth growth   { .num = 5, .den = 4 }; // 1.25x default (conservative for small vectors)
    sbo_layout       layout   { /*first as default, i.e. auto_select*/ };

    //! Give the inline arm a size field only as wide as N needs and spend what
    //! that frees on the inline buffer (see the embedded layout below for the
    //! capacities it buys). Off by default: the elements then start inside the
    //! bytes a size read covers, so every append stalls store-to-load
    //! forwarding. Worth it for buffers filled once and read repeatedly, not
    //! for append-heavy ones.
    bool             pack_inline_size{ false };
}; // struct sbo_options


// `embedded` spends the size field inside the union, where it overlaps the heap
// arm's own size; the other two spend a separate word on it outside the union,
// so they are never smaller at any N and are a word larger whenever the inline
// buffer would otherwise have fit within the heap-only footprint.
template <typename T, typename sz_t>
consteval sbo_layout resolve_layout( sbo_layout const l ) noexcept
{
    if ( l == sbo_layout::auto_select )
        return sbo_layout::embedded;
    return l;
}


////////////////////////////////////////////////////////////////////////////////
/// sbo_mixin -- shared VectorStorage logic for all union-based layouts.
///
/// Uses C++23 deducing this (P0847) to access layout-specific policy primitives
/// from the final sbo_hybrid<> specialization without an extra template
/// parameter.  Call sites in derived specializations declare:
///   friend sbo_mixin<T, N, sz_t, options>;
///
/// Required primitives from the derived class (all private, via friend):
///   bool is_heap()                                const noexcept
///   void set_inline_size( sz_t )                        noexcept
///   void set_heap_state ( T*, sz_t cap, sz_t sz )       noexcept
///   void set_size_preserving_flag( sz_t )               noexcept
///   void do_dec_size()                                  noexcept
///   void do_inc_size()                                  noexcept
///   T*         buffer_data()                            noexcept
///   T const*   buffer_data()                      const noexcept
///   T * __restrict       & heap_data_ref()              noexcept
///   T * __restrict const & heap_data_ref()        const noexcept
///   sz_t       & heap_cap_ref()                         noexcept
///   sz_t const & heap_cap_ref()                   const noexcept
///
/// The derived class must additionally provide (public):
///   sz_t size() const noexcept   (layout-specific bit manipulation)
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, sbo_options options>
class sbo_mixin
{
    static_assert( N > 0, "Use heap_storage for N=0" );

public:
    static std::uint8_t constexpr alignment{ options.alignment ? options.alignment : std::uint8_t{ alignof( std::conditional_t<complete<T>, T, std::max_align_t> ) } };

    static bool constexpr storage_zero_initialized{ false };
    static bool constexpr fixed_sized_copy        { false }; // heap-spill path makes unconditional whole-object copy invalid

    using size_type      = sz_t;
    using value_type     = T;
    using allocator_type = crt_allocator<T, sz_t>;

private:
    using al = allocator_type;

public:
    [[ nodiscard, gnu::pure ]] sz_t capacity( this auto const & self ) noexcept { return self.is_heap() ? self.heap_cap_ref () : sz_t{ N }; }
    [[ nodiscard, gnu::pure ]] auto data    ( this auto       & self ) noexcept { return self.is_heap() ? self.heap_data_ref() : self.buffer_data(); }
    [[ nodiscard, gnu::pure ]] bool empty   ( this auto const & self ) noexcept { return self.size() == 0; }

    void reserve( this auto & self, size_type const new_capacity )
    {
        if ( new_capacity > self.capacity() )
        {
            auto constexpr ceiling{ size_ceiling<decltype( self )>() };
            if ( new_capacity > ceiling ) [[ unlikely ]]
                detail::throw_length_error();
            self.grow_heap( new_capacity );
        }
    }

    // --- storage_* interface for vector<> ---

    // No __declspec( noalias ): clang derives nounwind from it, and the throws
    // on the spill path below then bypass the caller's handlers entirely.
    PSI_COLD
    value_type * storage_init( this auto & self, size_type const initial_size )
    {
        if ( initial_size <= N ) [[ likely ]]
        {
            self.set_inline_size( initial_size );
            return self.buffer_data();
        }
        auto constexpr ceiling{ size_ceiling<decltype( self )>() };
        if ( initial_size > ceiling ) [[ unlikely ]]
            detail::throw_length_error();
        auto const p{ al::template allocate<alignment>( initial_size ) };
        self.set_heap_state( p, initial_size, initial_size );
        return p;
    }

    template <geometric_growth G = {1, 1}>
    [[ gnu::returns_nonnull ]]
    value_type * storage_grow_to( this auto & self, size_type const target_size )
    {
        BOOST_ASSUME( target_size >= self.size() );
        auto const current_cap{ self.capacity() };
        if ( target_size > current_cap ) [[ unlikely ]]
        {
            auto constexpr ceiling{ size_ceiling<decltype( self )>() };
            if ( target_size > ceiling ) [[ unlikely ]]
                detail::throw_length_error();
            // The ceiling is itself a reachable size, so geometric overshoot
            // past it is clamped rather than refused.
            auto const wanted{ static_cast<bool>( G ) ? G( target_size, current_cap ) : target_size };
            self.grow_heap( std::min( wanted, ceiling ) );
        }
        self.set_size_preserving_flag( target_size );
        return self.data();
    }

    value_type * storage_shrink_to( this auto & self, size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= self.size() );
        if ( self.is_heap() )
        {
            self.heap_data_ref() = al::template shrink_to<alignment>( self.heap_data_ref(), self.size(), target_size );
            self.heap_cap_ref()  = target_size;
            BOOST_ASSUME( self.heap_data_ref() || !target_size );
        }
        self.set_size_preserving_flag( target_size );
        return self.data();
    }

    void storage_shrink_size_to( this auto & self, size_type const target_size ) noexcept
    {
        BOOST_ASSUME( self.size() >= target_size );
        self.set_size_preserving_flag( target_size );
    }

    void storage_dec_size( this auto & self ) noexcept { self.do_dec_size(); }
    void storage_inc_size( this auto & self ) noexcept { self.do_inc_size(); }

#ifdef _MSC_VER
    bool storage_try_expand_capacity( this auto & self, size_type const target_capacity ) noexcept
    requires( alignment <= detail::guaranteed_alignment )
    {
        if ( !self.is_heap() )
            return false;
        namespace bc = boost::container;
        al   alloc{};
        auto recv_size{ target_capacity };
        auto reuse    { self.heap_data_ref() };
        auto const result{ alloc.allocation_command(
            bc::expand_fwd | bc::nothrow_allocation,
            target_capacity, recv_size, reuse
        ) };
        if ( result )
        {
            self.heap_cap_ref() = recv_size;
            return true;
        }
        return false;
    }
#endif

    void storage_free( this auto & self ) noexcept
    {
        if ( self.is_heap() )
            al::template deallocate<alignment>( self.heap_data_ref(), self.heap_cap_ref() );
        self.set_inline_size( 0 );
    }

private:
    // Past this the size field cannot encode the size next to the heap flag,
    // and the write path only BOOST_ASSUMEs it (a no-op under NDEBUG): the
    // stored size would truncate while the elements are really there. Refused
    // on every capacity increase - all cold - which leaves
    // `capacity() <= max_size()` an invariant and the fast path unchecked.
    template <typename Self>
    [[ nodiscard ]] static constexpr size_type size_ceiling() noexcept { return std::remove_cvref_t<Self>::max_size(); }

    PSI_COLD [[ gnu::noinline, clang::preserve_most ]]
    void grow_heap( this auto & self, size_type const new_capacity )
    {
        auto constexpr ceiling{ size_ceiling<decltype( self )>() };
        BOOST_ASSUME( new_capacity > self.capacity() );
        BOOST_ASSUME( new_capacity <= ceiling );
        if ( self.is_heap() )
        {
            if constexpr ( is_trivially_moveable<T> )
            {
                self.heap_data_ref() = al::template grow_to<alignment>( self.heap_data_ref(), self.heap_cap_ref(), new_capacity );
            }
            else
            {
                // realloc is unsafe for non-trivially-moveable T: allocate+move+destroy+free
                auto * const old_ptr{ self.heap_data_ref() };
                auto   const old_cap{ self.heap_cap_ref()  };
                auto   const sz     { self.size()          };
                auto * const p      { al::template allocate<alignment>( new_capacity ) };
                std::uninitialized_move_n( old_ptr, sz, p );
                if constexpr ( !trivially_destructible_after_move_assignment<T> )
                    std::destroy_n( old_ptr, sz );
                al::template deallocate<alignment>( old_ptr, old_cap );
                self.heap_data_ref() = p;
            }
            self.heap_cap_ref() = new_capacity;
        }
        else
        {
            auto const sz{ self.size() };
            auto const p { al::template allocate<alignment>( new_capacity ) };
            if constexpr ( is_trivially_moveable<T> )
                std::memcpy( static_cast<void *>( p ), self.buffer_data(), sz * sizeof( T ) );
            else
            {
                std::uninitialized_move_n( self.buffer_data(), sz, p );
                if constexpr ( !trivially_destructible_after_move_assignment<T> )
                    std::destroy_n( self.buffer_data(), sz );
            }
            self.set_heap_state( p, new_capacity, sz );
        }
    }
}; // class sbo_mixin


////////////////////////////////////////////////////////////////////////////////
// sbo_hybrid -- forward declaration and three layout specializations.
//
// Each specialization inherits sbo_mixin (shared storage_* logic) and
// provides layout-specific policy primitives (bit manipulation, data member
// layout) plus:
//   - public size() const noexcept (layout-specific bit decode)
//   - default ctor  (inline-0 state)
//   - move ctor/assign (memcpy + reset source to inline-0)
//   - deleted copy ctor/assign  (element copy lives in vector<>)
//
// All union-based layouts are trivially relocatable (memcpy-safe): callers
// can safely memcpy or realloc the storage object itself.
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t = std::uint32_t, sbo_options options = {}>
class sbo_hybrid; // forward


////////////////////////////////////////////////////////////////////////////////
// Layout A: Compact (separate size_ after union, MSB = heap flag)
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, sbo_options options>
requires( resolve_layout<T, sz_t>( options.layout ) == sbo_layout::compact )
class sbo_hybrid<T, N, sz_t, options>
    :
    public sbo_mixin<T, N, sz_t, options>
{
    using mixin = sbo_mixin<T, N, sz_t, options>;
    friend mixin;

    static sz_t constexpr heap_flag   { sz_t{ 1 } << ( sizeof( sz_t ) * CHAR_BIT - 1 ) };
    static sz_t constexpr size_mask   { static_cast<sz_t>( ~heap_flag ) }; // ~ promotes to int for a narrow sz_t
    static sz_t constexpr max_size_val{ size_mask };

    static_assert( static_cast<std::uintmax_t>( N ) <= static_cast<std::uintmax_t>( max_size_val ), "inline capacity beyond what the size type can hold next to the heap flag" );

public:
    // The flag shares the size field, so the container cannot count as high as
    // the size type alone would allow. Without this the generic fallback in
    // `vector` reports the size type's full range and growth runs past what the
    // layout can represent.
    [[ nodiscard ]] static constexpr sz_t max_size() noexcept { return max_size_val; }

private:

    union data_t {
        constexpr  data_t() noexcept : buffer_{} {}
        constexpr ~data_t() noexcept {}

        struct {
            T * __restrict data_;
            sz_t           capacity_;
        } heap_;
        noninitialized_array<T, N> buffer_;
    };

    // --- policy interface (private, accessed via friend mixin) ---
    [[ nodiscard, gnu::pure ]] bool is_heap() const noexcept { return BOOST_UNLIKELY( ( size_ & heap_flag ) != 0 ); }

    void set_inline_size( sz_t const sz ) noexcept
    {
        BOOST_ASSUME( sz <= N );
        BOOST_ASSUME( !( sz & heap_flag ) );
        size_ = sz;
    }
    void set_heap_state( T * __restrict const p, sz_t const cap, sz_t const sz ) noexcept
    {
        BOOST_ASSUME( !( sz & heap_flag ) );
        storage_.heap_.data_     = p;
        storage_.heap_.capacity_ = cap;
        size_                    = sz | heap_flag;
    }
    void set_size_preserving_flag( sz_t const sz ) noexcept
    {
        BOOST_ASSUME( !( sz & heap_flag ) );
        size_ = sz | ( size_ & heap_flag );
    }
    void do_dec_size() noexcept { BOOST_ASSUME( this->size() >= 1 ); --size_; }
    void do_inc_size() noexcept { BOOST_ASSUME( this->size() < this->capacity() ); ++size_; }

    [[ nodiscard, gnu::pure ]] T       * buffer_data()       noexcept { return storage_.buffer_.data; }
    [[ nodiscard, gnu::pure ]] T const * buffer_data() const noexcept { return storage_.buffer_.data; }

    [[ nodiscard, gnu::pure ]] T * __restrict       & heap_data_ref()       noexcept { return storage_.heap_.data_; }
    [[ nodiscard, gnu::pure ]] T * __restrict const & heap_data_ref() const noexcept { return storage_.heap_.data_; }

    [[ nodiscard, gnu::pure ]] sz_t       & heap_cap_ref()       noexcept { return storage_.heap_.capacity_; }
    [[ nodiscard, gnu::pure ]] sz_t const & heap_cap_ref() const noexcept { return storage_.heap_.capacity_; }

public:
    [[ nodiscard, gnu::pure ]] sz_t size() const noexcept
    {
        auto const sz{ size_ & size_mask };
        BOOST_ASSUME( sz <= max_size_val );
        return sz;
    }

    sbo_hybrid() noexcept : storage_{}, size_{ 0 } {}
   ~sbo_hybrid() noexcept { this->storage_free(); }

    sbo_hybrid( sbo_hybrid const & ) = delete;
    sbo_hybrid & operator=( sbo_hybrid const & ) = delete;

    sbo_hybrid( sbo_hybrid && other ) noexcept( is_trivially_moveable<T> || std::is_nothrow_move_constructible_v<T> )
    {
        if constexpr ( is_trivially_moveable<T> )
        {
            std::memcpy( static_cast<void *>( this ), &other, sizeof( *this ) );
        }
        else if ( other.is_heap() )
        {
            storage_.heap_ = other.storage_.heap_;
            size_          = other.size_;
        }
        else
        {
            auto const sz{ other.size() };
            set_inline_size( sz );
            std::uninitialized_move_n( other.buffer_data(), sz, buffer_data() );
            if constexpr ( !trivially_destructible_after_move_assignment<T> )
                std::destroy_n( other.buffer_data(), sz );
        }
        other.size_ = 0; // inline-0: clear heap flag (heap pointer in other is now owned by *this)
    }

    sbo_hybrid & operator=( sbo_hybrid && other ) noexcept( is_trivially_moveable<T> || std::is_nothrow_move_constructible_v<T> )
    {
        this->storage_free();
        if constexpr ( is_trivially_moveable<T> )
        {
            std::memcpy( static_cast<void *>( this ), &other, sizeof( *this ) );
        }
        else if ( other.is_heap() )
        {
            storage_.heap_ = other.storage_.heap_;
            size_          = other.size_;
        }
        else
        {
            auto const sz{ other.size() };
            set_inline_size( sz );
            std::uninitialized_move_n( other.buffer_data(), sz, buffer_data() );
            if constexpr ( !trivially_destructible_after_move_assignment<T> )
                std::destroy_n( other.buffer_data(), sz );
        }
        other.size_ = 0;
        return *this;
    }

private:
    data_t storage_;
    sz_t   size_; // MSB = heap flag
}; // class sbo_hybrid (compact)


////////////////////////////////////////////////////////////////////////////////
// Layout B: Compact LSB (size-first, LSB flag)
//
// size_ comes first (before the union), LSB of size_ is the heap discriminant.
// On LE systems the flag is in byte 0 for optimal addressing.
// size() = size_ >> 1, is_heap() = size_ & 1.
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, sbo_options options>
requires( resolve_layout<T, sz_t>( options.layout ) == sbo_layout::compact_lsb )
class sbo_hybrid<T, N, sz_t, options>
    :
    public sbo_mixin<T, N, sz_t, options>
{
    using mixin = sbo_mixin<T, N, sz_t, options>;
    friend mixin;

    static sz_t constexpr max_size_val{ std::numeric_limits<sz_t>::max() >> 1 };

    static_assert( static_cast<std::uintmax_t>( N ) <= static_cast<std::uintmax_t>( max_size_val ), "inline capacity beyond what the size type can hold next to the heap flag" );

public:
    // The flag shares the size field, so the container cannot count as high as
    // the size type alone would allow. Without this the generic fallback in
    // `vector` reports the size type's full range and growth runs past what the
    // layout can represent.
    [[ nodiscard ]] static constexpr sz_t max_size() noexcept { return max_size_val; }

private:

    union data_t {
        constexpr  data_t() noexcept : buffer_{} {}
        constexpr ~data_t() noexcept {}

        struct {
            T * __restrict data_;
            sz_t           capacity_;
        } heap_;
        noninitialized_array<T, N> buffer_;
    };

    // --- policy interface ---
    [[ nodiscard, gnu::pure ]] bool is_heap() const noexcept { return BOOST_UNLIKELY( ( size_ & 1 ) != 0 ); }

    void set_inline_size( sz_t const sz ) noexcept
    {
        BOOST_ASSUME( sz <= N );
        size_ = static_cast<sz_t>( sz << 1 ); // LSB = 0 -> inline
    }
    void set_heap_state( T * __restrict const p, sz_t const cap, sz_t const sz ) noexcept
    {
        BOOST_ASSUME( sz <= max_size_val );
        storage_.heap_.data_     = p;
        storage_.heap_.capacity_ = cap;
        size_                    = static_cast<sz_t>( ( sz << 1 ) | 1 ); // LSB = 1 -> heap
    }
    void set_size_preserving_flag( sz_t const sz ) noexcept
    {
        BOOST_ASSUME( sz <= max_size_val );
        size_ = static_cast<sz_t>( ( sz << 1 ) | ( size_ & 1 ) );
    }
    void do_dec_size() noexcept { BOOST_ASSUME( this->size() >= 1 ); size_ -= 2; }
    void do_inc_size() noexcept { BOOST_ASSUME( this->size() < this->capacity() ); size_ += 2; }

    [[ nodiscard, gnu::pure ]] T       * buffer_data()       noexcept { return storage_.buffer_.data; }
    [[ nodiscard, gnu::pure ]] T const * buffer_data() const noexcept { return storage_.buffer_.data; }

    [[ nodiscard, gnu::pure ]] T * __restrict       & heap_data_ref()       noexcept { return storage_.heap_.data_; }
    [[ nodiscard, gnu::pure ]] T * __restrict const & heap_data_ref() const noexcept { return storage_.heap_.data_; }

    [[ nodiscard, gnu::pure ]] sz_t       & heap_cap_ref()       noexcept { return storage_.heap_.capacity_; }
    [[ nodiscard, gnu::pure ]] sz_t const & heap_cap_ref() const noexcept { return storage_.heap_.capacity_; }

public:
    [[ nodiscard, gnu::pure ]] sz_t size() const noexcept
    {
        auto const sz{ static_cast<sz_t>( size_ >> 1 ) };
        BOOST_ASSUME( sz <= max_size_val );
        return sz;
    }

    sbo_hybrid() noexcept : size_{ 0 }, storage_{} {}
   ~sbo_hybrid() noexcept { this->storage_free(); }

    sbo_hybrid( sbo_hybrid const & ) = delete;
    sbo_hybrid & operator=( sbo_hybrid const & ) = delete;

    sbo_hybrid( sbo_hybrid && other ) noexcept( is_trivially_moveable<T> || std::is_nothrow_move_constructible_v<T> )
    {
        if constexpr ( is_trivially_moveable<T> )
        {
            std::memcpy( static_cast<void *>( this ), &other, sizeof( *this ) );
        }
        else if ( other.is_heap() )
        {
            size_          = other.size_;
            storage_.heap_ = other.storage_.heap_;
        }
        else
        {
            auto const sz{ other.size() };
            set_inline_size( sz );
            std::uninitialized_move_n( other.buffer_data(), sz, buffer_data() );
            if constexpr ( !trivially_destructible_after_move_assignment<T> )
                std::destroy_n( other.buffer_data(), sz );
        }
        other.size_ = 0; // LSB=0, sz=0 -> inline-0
    }

    sbo_hybrid & operator=( sbo_hybrid && other ) noexcept( is_trivially_moveable<T> || std::is_nothrow_move_constructible_v<T> )
    {
        this->storage_free();
        if constexpr ( is_trivially_moveable<T> )
        {
            std::memcpy( static_cast<void *>( this ), &other, sizeof( *this ) );
        }
        else if ( other.is_heap() )
        {
            size_          = other.size_;
            storage_.heap_ = other.storage_.heap_;
        }
        else
        {
            auto const sz{ other.size() };
            set_inline_size( sz );
            std::uninitialized_move_n( other.buffer_data(), sz, buffer_data() );
            if constexpr ( !trivially_destructible_after_move_assignment<T> )
                std::destroy_n( other.buffer_data(), sz );
        }
        other.size_ = 0;
        return *this;
    }

private:
    sz_t   size_;    // LSB = heap flag, actual size = size_ >> 1
    data_t storage_;
}; // class sbo_hybrid (compact_lsb)


namespace detail
{
    template <std::uintmax_t Max>
    using narrowest_uint_t = std::conditional_t
    <
        ( Max <= 0xFFu ), std::uint8_t, std::conditional_t
        <
            ( Max <= 0xFFFFu ), std::uint16_t, std::conditional_t
            <
                ( Max <= 0xFFFFFFFFu ), std::uint32_t, std::uint64_t
            >
        >
    >;
} // namespace detail


////////////////////////////////////////////////////////////////////////////////
// Layout C: Embedded (sz_ packed inside union, LSB flag)
//
// Both union variants start their size field at offset 0 and carry the heap
// flag in its bit 0, so the flag is readable without knowing which arm is live.
// The inline arm cannot count past N, so it takes only the width N needs -- the
// bytes that buys go to the inline buffer. No external size_ field.
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, sbo_options options>
requires( resolve_layout<T, sz_t>( options.layout ) == sbo_layout::embedded )
class sbo_hybrid<T, N, sz_t, options>
    :
    public sbo_mixin<T, N, sz_t, options>
{
    using mixin = sbo_mixin<T, N, sz_t, options>;
    friend mixin;

    static sz_t constexpr max_size_val{ std::numeric_limits<sz_t>::max() >> 1 };

    static_assert( static_cast<std::uintmax_t>( N ) <= static_cast<std::uintmax_t>( max_size_val ), "inline capacity beyond what the size type can hold next to the heap flag" );

    // The inline size field is narrowed only where that actually moves the
    // elements closer to offset 0 -- i.e. where alignof( T ) does not already
    // pad the field back out to sizeof( sz_t ). Everywhere else the two arms
    // keep the same size type and every accessor below stays a plain shift.
    using narrow_sz_t = detail::narrowest_uint_t<std::uintmax_t{ N } * 2>;
    static std::size_t constexpr elem_align{ alignof( std::conditional_t<complete<T>, T, std::max_align_t> ) };
    static std::size_t constexpr narrow_elements_offset{ ( sizeof( narrow_sz_t ) + elem_align - 1 ) / elem_align * elem_align };
    static std::size_t constexpr wide_elements_offset  { ( sizeof( sz_t        ) + elem_align - 1 ) / elem_align * elem_align };

    static bool constexpr narrow_inline_size{ options.pack_inline_size && ( narrow_elements_offset < wide_elements_offset ) };

    using inline_sz_t = std::conditional_t<narrow_inline_size, narrow_sz_t, sz_t>;

    //! Every bit of the inline arm's size field, flag included.
    static sz_t constexpr inline_field_mask{ static_cast<sz_t>( std::numeric_limits<inline_sz_t>::max() ) };

public:
    // The flag shares the size field, so the container cannot count as high as
    // the size type alone would allow. Without this the generic fallback in
    // `vector` reports the size type's full range and growth runs past what the
    // layout can represent.
    [[ nodiscard ]] static constexpr sz_t max_size() noexcept { return max_size_val; }

private:

    union data_t {
        // Named struct with no-op ctor -- avoids overwriting heap state when
        // inherited constructors re-default-initialize data members after the
        // base-class body.
        struct inline_t {
            inline_sz_t                sz_;       // LSB=0, actual size = sz_ >> 1
            noninitialized_array<T, N> elements_;
            constexpr  inline_t() noexcept {}     // no-op: leaves sz_ uninitialized
            constexpr ~inline_t() noexcept {}
        } inline_;
        struct {
            sz_t           sz_;       // LSB=1, actual size = sz_ >> 1
            sz_t           cap_;
            T * __restrict data_;
        } heap_;

        constexpr  data_t() noexcept : inline_{} {} // no-op
        constexpr ~data_t() noexcept {}
    };

    //! The leading size field, always at the heap arm's width -- reads and
    //! writes have to agree on one width or every append pays a store-to-load
    //! forwarding stall.
    //!
    //! Where the arms narrow the field they give it different types, so there
    //! is no common initial sequence to read through and this goes to the
    //! object representation instead. Inline, the bytes it covers past the
    //! field are element storage: reads mask them off and writes put them back
    //! byte for byte, so no element ever changes value.
    [[ nodiscard, gnu::pure ]] sz_t lead_word() const noexcept
    {
        if constexpr ( narrow_inline_size )
        {
            sz_t word;
            std::memcpy( &word, &storage_, sizeof( word ) );
            return word;
        }
        else
            return storage_.heap_.sz_;
    }
    void set_lead_word( sz_t const word ) noexcept
    {
        if constexpr ( narrow_inline_size )
            std::memcpy( &storage_, &word, sizeof( word ) );
        else
            storage_.heap_.sz_ = word;
    }

    //! `word` with the inline arm's field replaced and its surplus bytes kept.
    [[ nodiscard ]] static sz_t inline_field_into( sz_t const word, sz_t const sz ) noexcept
    {
        return static_cast<sz_t>( ( word & static_cast<sz_t>( ~inline_field_mask ) ) | ( sz << 1 ) );
    }

    // --- policy interface ---
    [[ nodiscard, gnu::pure ]] bool is_heap() const noexcept { return BOOST_UNLIKELY( ( lead_word() & 1 ) != 0 ); }

    void set_inline_size( sz_t const sz ) noexcept
    {
        BOOST_ASSUME( sz <= N );
        if constexpr ( narrow_inline_size )
            set_lead_word( inline_field_into( lead_word(), sz ) ); // LSB = 0 -> inline
        else
            storage_.inline_.sz_ = static_cast<inline_sz_t>( sz << 1 );
    }
    void set_heap_state( T * __restrict const p, sz_t const cap, sz_t const sz ) noexcept
    {
        BOOST_ASSUME( sz <= max_size_val );
        set_lead_word( static_cast<sz_t>( ( sz << 1 ) | 1 ) ); // LSB = 1 -> heap
        storage_.heap_.cap_  = cap;
        storage_.heap_.data_ = p;
    }
    void set_size_preserving_flag( sz_t const sz ) noexcept
    {
        BOOST_ASSUME( sz <= max_size_val );
        auto const word{ lead_word() };
        if constexpr ( narrow_inline_size )
            set_lead_word( ( word & 1 ) ? static_cast<sz_t>( ( sz << 1 ) | 1 ) : inline_field_into( word, sz ) );
        else
            set_lead_word( static_cast<sz_t>( ( sz << 1 ) | ( word & 1 ) ) );
    }
    // The encoded size tops out at 2N, which is what the inline field was sized
    // to hold, so one full-width step never carries out of the live arm's field.
    void do_dec_size() noexcept { BOOST_ASSUME( this->size() >= 1 );               set_lead_word( static_cast<sz_t>( lead_word() - 2 ) ); }
    void do_inc_size() noexcept { BOOST_ASSUME( this->size() < this->capacity() ); set_lead_word( static_cast<sz_t>( lead_word() + 2 ) ); }

    [[ nodiscard, gnu::pure ]] T       * buffer_data()       noexcept { return storage_.inline_.elements_.data; }
    [[ nodiscard, gnu::pure ]] T const * buffer_data() const noexcept { return storage_.inline_.elements_.data; }

    [[ nodiscard, gnu::pure ]] T * __restrict       & heap_data_ref()       noexcept { return storage_.heap_.data_; }
    [[ nodiscard, gnu::pure ]] T * __restrict const & heap_data_ref() const noexcept { return storage_.heap_.data_; }

    [[ nodiscard, gnu::pure ]] sz_t       & heap_cap_ref()       noexcept { return storage_.heap_.cap_; }
    [[ nodiscard, gnu::pure ]] sz_t const & heap_cap_ref() const noexcept { return storage_.heap_.cap_; }

public:
    // Branch-free: the flag sits at the same bit in both arms, so the shift is
    // the same in both and only the mask that drops the wider arm's surplus
    // bits is picked -- from the flag itself, as a select rather than a branch.
    [[ nodiscard, gnu::pure ]] sz_t size() const noexcept
    {
        auto const raw{ lead_word() };
        auto const sz
        {
            [ raw ]
            {
                auto const decoded{ static_cast<sz_t>( raw >> 1 ) };
                if constexpr ( narrow_inline_size )
                    return ( raw & 1 ) ? decoded : static_cast<sz_t>( decoded & ( inline_field_mask >> 1 ) );
                else
                    return decoded;
            }()
        };
        BOOST_ASSUME( sz <= max_size_val );
        return sz;
    }

    //! Emptiness without decoding the size: the narrow layout's decode is a
    //! select, while each arm's empty word is recognisable on its own -- an
    //! empty inline arm is all-zero across its field (the surplus bits are
    //! elements, so they may not be looked at), and an empty heap arm is the
    //! bare flag. Neither test can fire on the other arm: a heap word always
    //! has the flag bit set, and an inline word never equals it.
    [[ nodiscard, gnu::pure ]] bool storage_empty() const noexcept
    {
        auto const raw{ lead_word() };
        if constexpr ( narrow_inline_size )
            return ( ( raw & inline_field_mask ) == 0 ) | ( raw == 1 );
        else
            return raw < 2; // flag | size, so "no size bits set" is the whole test
    }

    sbo_hybrid() noexcept { set_lead_word( 0 ); } // nothing constructed yet: the whole word can go at once
   ~sbo_hybrid() noexcept { this->storage_free(); }

    sbo_hybrid( sbo_hybrid const & ) = delete;
    sbo_hybrid & operator=( sbo_hybrid const & ) = delete;

    sbo_hybrid( sbo_hybrid && other ) noexcept( is_trivially_moveable<T> || std::is_nothrow_move_constructible_v<T> )
    {
        if constexpr ( is_trivially_moveable<T> )
        {
            std::memcpy( static_cast<void *>( this ), &other, sizeof( *this ) );
        }
        else if ( other.is_heap() )
        {
            storage_.heap_ = other.storage_.heap_;
        }
        else
        {
            auto const sz{ other.size() };
            set_inline_size( sz );
            std::uninitialized_move_n( other.buffer_data(), sz, buffer_data() );
            if constexpr ( !trivially_destructible_after_move_assignment<T> )
                std::destroy_n( other.buffer_data(), sz );
        }
        other.set_inline_size( 0 ); // inline-0: clear LSB flag + size
    }

    sbo_hybrid & operator=( sbo_hybrid && other ) noexcept( is_trivially_moveable<T> || std::is_nothrow_move_constructible_v<T> )
    {
        this->storage_free();
        if constexpr ( is_trivially_moveable<T> )
        {
            std::memcpy( static_cast<void *>( this ), &other, sizeof( *this ) );
        }
        else if ( other.is_heap() )
        {
            storage_.heap_ = other.storage_.heap_;
        }
        else
        {
            auto const sz{ other.size() };
            set_inline_size( sz );
            std::uninitialized_move_n( other.buffer_data(), sz, buffer_data() );
            if constexpr ( !trivially_destructible_after_move_assignment<T> )
                std::destroy_n( other.buffer_data(), sz );
        }
        other.set_inline_size( 0 );
        return *this;
    }

private:
    data_t storage_; // sole data member -- no external size_ field
}; // class sbo_hybrid (embedded)


////////////////////////////////////////////////////////////////////////////////
// is_trivially_moveable: all union-based layouts memcpy-safe when T is.
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, sbo_options opts>
bool constexpr is_trivially_moveable<sbo_hybrid<T, N, sz_t, opts>>{ is_trivially_moveable<T> };

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
