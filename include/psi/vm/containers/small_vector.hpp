////////////////////////////////////////////////////////////////////////////////
/// Small vector — inline (stack) buffer with heap spill
///
/// Four layout modes selectable via NTTP options:
///  - auto_select (default): picks the best layout based on T and sz_t.
///    Resolves to compact_lsb when sizeof(sz_t) > alignof(T), else embedded.
///  - compact: union-based, MSB-of-size flag, trivially relocatable.
///  - compact_lsb: union-based, size-first with LSB flag, trivially relocatable.
///  - embedded: union-based, size inside union (LSB flag, common initial
///    sequence), trivially relocatable. No external size field — sometimes
///    smaller sizeof than compact_lsb, never larger.
///  - pointer_based: Boost/LLVM SmallVector style — type-erasable across N
///    values via small_vector_base<T>, trivial data() getter, but NOT trivially
///    relocatable (data_ points to inline buffer when small).
///
/// All layouts require is_trivially_moveable<T> (memcpy/realloc for moves).
/// Built on vector_impl CRTP base, reuses crt_aligned_allocator for heap path.
////////////////////////////////////////////////////////////////////////////////
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

#include <psi/vm/containers/noninitialized_array.hpp>
#include <psi/vm/containers/tr_vector.hpp> // crt_aligned_allocator, vector_impl

#include <boost/assert.hpp>

#include <climits>
#include <cstring>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

enum class small_vector_layout : std::uint8_t {
    auto_select,   // default — resolved to best layout based on T and sz_t
    compact,       // union-based, MSB-of-size flag, trivially relocatable
    compact_lsb,   // union-based, size-first with LSB flag, trivially relocatable
    embedded,      // union-based, size inside union (LSB flag), trivially relocatable
    pointer_based, // LLVM/Boost style, type-erasable, NOT trivially relocatable
};

struct small_vector_options
{
    std::uint8_t        alignment{ 0 };
    geometric_growth    growth   { .num = 5, .den = 4 }; // 1.25x default (conservative for small vectors)
    small_vector_layout layout   { /*first as default*/ };
}; // struct small_vector_options


////////////////////////////////////////////////////////////////////////////////
// auto_select resolution
////////////////////////////////////////////////////////////////////////////////

template <typename T, typename sz_t>
consteval small_vector_layout resolve_layout( small_vector_layout const l ) noexcept
{
    if ( l != small_vector_layout::auto_select )
        return l;
    return ( sizeof( sz_t ) > alignof( T ) )
        ? small_vector_layout::compact_lsb
        : small_vector_layout::embedded;
}


////////////////////////////////////////////////////////////////////////////////
// Forward declarations
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, small_vector_options options>
class sv_union_base;

template <typename T, std::uint32_t N, typename sz_t = std::uint32_t, small_vector_options options = {}>
class small_vector;

// pointer_based layout base (N-independent)
template <typename T, typename sz_t, small_vector_options options>
requires( options.layout == small_vector_layout::pointer_based && is_trivially_moveable<T> )
class small_vector_base;


////////////////////////////////////////////////////////////////////////////////
// sv_union_base — deducing-this mixin for all union-based layouts
//
// Sits between vector_impl and the final layout class. Contains all logic
// shared across compact, compact_lsb, and embedded layouts. Uses C++23
// deducing this (P0847) to access the final layout class — no Derived
// template parameter needed.
//
// Required interface from the final layout class:
//   bool is_heap() const noexcept
//   sz_t size() const noexcept
//   void set_inline_size( sz_t ) noexcept
//   void set_heap_state( T*, sz_t cap, sz_t sz ) noexcept
//   void set_size_preserving_flag( sz_t ) noexcept
//   void do_dec_size() noexcept
//   void do_inc_size() noexcept
//   T*       buffer_data()       noexcept
//   T const* buffer_data() const noexcept
//   T* __restrict&       heap_data_ref()       noexcept
//   T* __restrict const& heap_data_ref() const noexcept
//   sz_t&       heap_cap_ref()       noexcept
//   sz_t const& heap_cap_ref() const noexcept
//   static constexpr sz_t max_size_val
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, small_vector_options options>
class sv_union_base
    :
    public vector_impl<small_vector<T, N, sz_t, options>, T, sz_t>
{
    static_assert( N > 0, "Use tr_vector for N=0" );

public:
    static std::uint8_t constexpr alignment{ options.alignment ? options.alignment : std::uint8_t{ alignof( std::conditional_t<complete<T>, T, std::max_align_t> ) } };

    static bool constexpr storage_zero_initialized{ false };

    using size_type      = sz_t;
    using value_type     = T;
    using allocator_type = crt_aligned_allocator<T, sz_t, alignment>;

private:
    using al      = allocator_type;
    using vi_base = vector_impl<small_vector<T, N, sz_t, options>, T, sz_t>;
    friend vi_base;

public:
    using vi_base::vi_base;

    [[ nodiscard, gnu::pure ]] sz_t capacity( this auto const & self ) noexcept
    {
        return self.is_heap() ? self.heap_cap_ref() : sz_t{ N };
    }

    [[ nodiscard, gnu::pure ]] auto data( this auto & self ) noexcept { return self.is_heap() ? self.heap_data_ref() : self.buffer_data(); }

    void reserve( this auto & self, size_type const new_capacity )
    {
        if ( new_capacity > self.capacity() )
            self.grow_heap( new_capacity );
    }

protected:
    // Helper bodies for derived-class copy/move/dtor one-liners.

    void copy_init_from( this auto & self, auto const & other )
    {
        auto const sz { other.size() };
        auto const dst{ self.storage_init( sz ) };
        try { std::uninitialized_copy_n( other.data(), sz, dst ); }
        catch( ... ) { self.storage_free(); throw; }
    }

    void move_init_from( this auto & self, auto && other ) noexcept
    {
        auto const sz{ other.size() };
        if ( other.is_heap() )
        {
            self.set_heap_state( other.heap_data_ref(), other.heap_cap_ref(), sz );
        }
        else
        {
            std::memcpy( static_cast<void *>( self.buffer_data() ), other.buffer_data(), sz * sizeof( T ) );
            self.set_inline_size( sz );
        }
        other.set_inline_size( 0 );
    }

    void move_assign_from( this auto & self, auto && other ) noexcept
    {
        self.clear();
        if ( self.is_heap() )
            al::deallocate( self.heap_data_ref(), self.heap_cap_ref() );
        auto const sz{ other.size() };
        if ( other.is_heap() )
        {
            self.set_heap_state( other.heap_data_ref(), other.heap_cap_ref(), sz );
        }
        else
        {
            std::memcpy( static_cast<void *>( self.buffer_data() ), other.buffer_data(), sz * sizeof( T ) );
            self.set_inline_size( sz );
        }
        other.set_inline_size( 0 );
    }

    void destroy_and_free( this auto & self ) noexcept
    {
        std::destroy_n( self.data(), self.size() );
        if ( self.is_heap() )
            al::deallocate( self.heap_data_ref(), self.heap_cap_ref() );
    }

private:
    // vector_impl storage interface

    [[ gnu::cold ]]
#ifdef _MSC_VER
    __declspec( noalias )
#endif
    constexpr value_type * storage_init( this auto & self, size_type const initial_size )
    {
        if ( initial_size > N )
        {
            auto const p{ al::allocate( initial_size ) };
            self.set_heap_state( p, initial_size, initial_size );
            return p;
        }
        self.set_inline_size( initial_size );
        return self.buffer_data();
    }

    [[ gnu::returns_nonnull ]]
    constexpr value_type * storage_grow_to( this auto & self, size_type const target_size )
    {
        auto const current_cap{ self.capacity() };
        BOOST_ASSUME( target_size >= self.size() );
        if ( target_size > current_cap ) [[ unlikely ]]
        {
            self.grow_heap( options.growth( target_size, current_cap ) );
        }
        self.set_size_preserving_flag( target_size );
        return self.data();
    }

    constexpr value_type * storage_shrink_to( this auto & self, size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= self.size() );
        if ( self.is_heap() )
        {
            self.heap_data_ref() = al::shrink_to( self.heap_data_ref(), self.size(), target_size );
            self.heap_cap_ref()  = target_size;
            BOOST_ASSUME( self.heap_data_ref() || !target_size );
        }
        self.set_size_preserving_flag( target_size );
        return self.data();
    }

    constexpr void storage_shrink_size_to( this auto & self, size_type const target_size ) noexcept
    {
        BOOST_ASSUME( self.size() >= target_size );
        self.set_size_preserving_flag( target_size );
    }

    // storage_dec_size and storage_inc_size are layout-specific — forwarded.
    // Named do_* in layout classes to avoid hiding this mixin's methods
    // (vector_impl calls self.storage_dec_size() → finds this method,
    //  which then calls self.do_dec_size() → finds the layout's version).
    void storage_dec_size( this auto & self ) noexcept { self.do_dec_size(); }
    void storage_inc_size( this auto & self ) noexcept { self.do_inc_size(); }

#ifdef _MSC_VER
    bool storage_try_expand_capacity( this auto & self, size_type const target_capacity ) noexcept
    requires( alignment <= detail::guaranteed_alignment )
    {
        if ( !self.is_heap() )
            return false;
        namespace bc = boost::container;
        auto recv_size{ target_capacity };
        auto reuse    { self.heap_data_ref() };
        auto const result{ al::allocation_command(
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
            al::deallocate( self.heap_data_ref(), self.heap_cap_ref() );
        self.set_inline_size( 0 );
    }

    [[ gnu::cold, gnu::noinline, clang::preserve_most ]]
    void grow_heap( this auto & self, size_type const new_capacity )
    {
        BOOST_ASSUME( new_capacity > self.capacity() );
        if ( self.is_heap() )
        {
            self.heap_data_ref() = al::grow_to( self.heap_data_ref(), self.heap_cap_ref(), new_capacity );
            self.heap_cap_ref()  = new_capacity;
        }
        else
        {
            auto const sz{ self.size() };
            auto const p { al::allocate( new_capacity ) };
            std::memcpy( static_cast<void *>( p ), self.buffer_data(), sz * sizeof( T ) );
            self.set_heap_state( p, new_capacity, sz );
        }
    }
}; // class sv_union_base


////////////////////////////////////////////////////////////////////////////////
// Layout A: Compact (union-based, MSB flag) — trivially relocatable
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, small_vector_options options>
requires( resolve_layout<T, sz_t>( options.layout ) == small_vector_layout::compact && is_trivially_moveable<T> )
class [[ nodiscard, clang::trivial_abi ]] small_vector<T, N, sz_t, options>
    :
    public sv_union_base<T, N, sz_t, options>
{
    using mixin = sv_union_base<T, N, sz_t, options>;
    friend mixin;

    static sz_t constexpr heap_flag   { sz_t{ 1 } << ( sizeof( sz_t ) * CHAR_BIT - 1 ) };
    static sz_t constexpr size_mask   { ~heap_flag };

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
    [[ nodiscard, gnu::pure ]] bool is_heap() const noexcept { return ( size_ & heap_flag ) != 0; }

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

    void do_dec_size() noexcept { BOOST_ASSUME( size() >= 1 ); --size_; }
    void do_inc_size() noexcept { BOOST_ASSUME( size() < this->capacity() ); ++size_; }

    [[ nodiscard, gnu::pure ]] T       * buffer_data()       noexcept { return storage_.buffer_.data; }
    [[ nodiscard, gnu::pure ]] T const * buffer_data() const noexcept { return storage_.buffer_.data; }

    [[ nodiscard, gnu::pure ]] T * __restrict & heap_data_ref()       noexcept { return storage_.heap_.data_; }
    [[ nodiscard, gnu::pure ]] T * __restrict const & heap_data_ref() const noexcept { return storage_.heap_.data_; }

    [[ nodiscard, gnu::pure ]] sz_t       & heap_cap_ref()       noexcept { return storage_.heap_.capacity_; }
    [[ nodiscard, gnu::pure ]] sz_t const & heap_cap_ref() const noexcept { return storage_.heap_.capacity_; }

    static sz_t constexpr max_size_val{ size_mask };

public:
    using mixin::mixin;

    constexpr small_vector() noexcept : size_{ 0 } {}

    constexpr small_vector( small_vector const & other ) : mixin{} { this->copy_init_from( other ); }
    constexpr small_vector( small_vector && other ) noexcept : mixin{} { this->move_init_from( std::move( other ) ); }
    constexpr small_vector & operator=( small_vector const & other ) { return ( *this = small_vector( other ) ); }
    constexpr small_vector & operator=( small_vector && other ) noexcept { this->move_assign_from( std::move( other ) ); return *this; }
    constexpr small_vector & operator=( std::initializer_list<T> const data ) { this->assign( data ); return *this; }
    constexpr ~small_vector() noexcept { this->destroy_and_free(); }

    [[ nodiscard, gnu::pure ]] sz_t size() const noexcept
    {
        auto const sz{ size_ & size_mask };
        BOOST_ASSUME( sz <= max_size_val );
        return sz;
    }

private:
    data_t storage_;
    sz_t   size_; // MSB = heap flag
}; // class small_vector (compact)


////////////////////////////////////////////////////////////////////////////////
// Layout B: Pointer-based — type-erasable
////////////////////////////////////////////////////////////////////////////////

template <typename T, typename sz_t, small_vector_options options>
requires( options.layout == small_vector_layout::pointer_based && is_trivially_moveable<T> )
class [[ nodiscard ]] small_vector_base
    :
    public vector_impl<small_vector_base<T, sz_t, options>, T, sz_t>
{
public:
    static std::uint8_t constexpr alignment{ options.alignment ? options.alignment : std::uint8_t{ alignof( std::conditional_t<complete<T>, T, std::max_align_t> ) } };

    static bool constexpr storage_zero_initialized{ false };

    using size_type      = sz_t;
    using value_type     = T;
    using allocator_type = crt_aligned_allocator<T, sz_t, alignment>;

private:
    using al   = allocator_type;
    using base = vector_impl<small_vector_base, T, sz_t>;

protected:
    [[ nodiscard, gnu::pure ]] T * first_el() noexcept
    {
        auto const raw{ reinterpret_cast<char *>( this ) + sizeof( small_vector_base ) };
        return reinterpret_cast<T *>( ( reinterpret_cast<std::uintptr_t>( raw ) + alignof( T ) - 1 ) & ~( alignof( T ) - 1 ) );
    }
    [[ nodiscard, gnu::pure ]] T const * first_el() const noexcept { return const_cast<small_vector_base *>( this )->first_el(); }

    void reset_to_small( sz_t const inline_cap ) noexcept
    {
        data_     = first_el();
        size_     = 0;
        capacity_ = inline_cap;
    }

public:
    [[ nodiscard, gnu::pure ]] bool is_small() const noexcept { return data_ == first_el(); }

    [[ nodiscard, gnu::pure ]] sz_t size    () const noexcept { return size_;     }
    [[ nodiscard, gnu::pure ]] sz_t capacity() const noexcept { return capacity_; }

    [[ nodiscard, gnu::pure ]] value_type       * data()       noexcept { return data_; }
    [[ nodiscard, gnu::pure ]] value_type const * data() const noexcept { return data_; }

    void reserve( size_type const new_capacity )
    {
        if ( new_capacity > capacity_ )
            grow_heap( new_capacity );
    }

private: friend base;
    template <typename, std::uint32_t, typename, small_vector_options> friend class small_vector;

    [[ gnu::cold ]]
#ifdef _MSC_VER
    __declspec( noalias )
#endif
    constexpr value_type * storage_init( size_type const initial_size )
    {
        if ( initial_size > capacity_ )
        {
            data_     = al::allocate( initial_size );
            size_     = initial_size;
            capacity_ = initial_size;
            return data_;
        }
        size_ = initial_size;
        return data_;
    }

    constexpr value_type * storage_grow_to( size_type const target_size )
    {
        BOOST_ASSUME( target_size >= size_ );
        if ( target_size > capacity_ ) [[ unlikely ]]
        {
            grow_heap( options.growth( target_size, capacity_ ) );
        }
        size_ = target_size;
        return data_;
    }

    constexpr value_type * storage_shrink_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= size_ );
        if ( !is_small() )
        {
            data_     = al::shrink_to( data_, size_, target_size );
            capacity_ = target_size;
            BOOST_ASSUME( data_ || !target_size );
        }
        size_ = target_size;
        return data_;
    }

    constexpr void storage_shrink_size_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( size_ >= target_size );
        size_ = target_size;
    }

    void storage_dec_size() noexcept { BOOST_ASSUME( size_ >= 1 ); --size_; }
    void storage_inc_size() noexcept { BOOST_ASSUME( size_ < capacity_ ); ++size_; }

#ifdef _MSC_VER
    bool storage_try_expand_capacity( size_type const target_capacity ) noexcept
    requires( alignment <= detail::guaranteed_alignment )
    {
        if ( is_small() )
            return false;
        namespace bc = boost::container;
        auto recv_size{ target_capacity };
        auto reuse    { data_ };
        auto const result{ al::allocation_command(
            bc::expand_fwd | bc::nothrow_allocation,
            target_capacity, recv_size, reuse
        ) };
        if ( result )
        {
            capacity_ = recv_size;
            return true;
        }
        return false;
    }
#endif

    void storage_free() noexcept
    {
        if ( !is_small() )
            al::deallocate( data_, capacity_ );
    }

private:
    [[ gnu::cold, gnu::noinline, clang::preserve_most ]]
    void grow_heap( size_type const new_capacity )
    {
        BOOST_ASSUME( new_capacity > capacity_ );
        if ( !is_small() )
        {
            data_     = al::grow_to( data_, capacity_, new_capacity );
            capacity_ = new_capacity;
        }
        else
        {
            auto const p{ al::allocate( new_capacity ) };
            std::memcpy( static_cast<void *>( p ), data_, size_ * sizeof( T ) );
            data_     = p;
            capacity_ = new_capacity;
        }
    }

protected:
    T * __restrict data_;
    sz_t           size_;
    sz_t           capacity_;
}; // class small_vector_base


template <typename T, std::uint32_t N, typename sz_t, small_vector_options options>
requires( options.layout == small_vector_layout::pointer_based && is_trivially_moveable<T> )
class [[ nodiscard ]] small_vector<T, N, sz_t, options>
    :
    public small_vector_base<T, sz_t, options>
{
    static_assert( N > 0, "Use tr_vector for N=0" );

    using ptr_base = small_vector_base<T, sz_t, options>;

public:
    using size_type  = sz_t;
    using value_type = T;

    constexpr small_vector() noexcept { ptr_base::reset_to_small( N ); }

    constexpr small_vector( small_vector const & other ) : ptr_base{}
    {
        ptr_base::reset_to_small( N );
        auto const sz{ other.size() };
        auto const dst{ this->storage_init( sz ) };
        try { std::uninitialized_copy_n( other.data(), sz, dst ); }
        catch( ... ) { this->storage_free(); ptr_base::reset_to_small( N ); throw; }
    }

    constexpr small_vector( small_vector && other ) noexcept : ptr_base{}
    {
        auto const sz{ other.size() };
        if ( other.is_small() )
        {
            ptr_base::reset_to_small( N );
            std::memcpy( static_cast<void *>( buffer_.data ), other.data(), sz * sizeof( T ) );
            this->size_ = sz;
        }
        else
        {
            this->data_     = other.data_;
            this->size_     = sz;
            this->capacity_ = other.capacity_;
        }
        other.reset_to_small( N );
    }

    constexpr small_vector & operator=( small_vector const & other )
    {
        return ( *this = small_vector( other ) );
    }

    constexpr small_vector & operator=( small_vector && other ) noexcept
    {
        this->clear();
        if ( !this->is_small() )
            ptr_base::allocator_type::deallocate( this->data_, this->capacity_ );
        auto const sz{ other.size() };
        if ( other.is_small() )
        {
            ptr_base::reset_to_small( N );
            std::memcpy( static_cast<void *>( buffer_.data ), other.data(), sz * sizeof( T ) );
            this->size_ = sz;
        }
        else
        {
            this->data_     = other.data_;
            this->size_     = sz;
            this->capacity_ = other.capacity_;
        }
        other.reset_to_small( N );
        return *this;
    }

    constexpr small_vector & operator=( std::initializer_list<value_type> const data ) { this->assign( data ); return *this; }

    constexpr ~small_vector() noexcept
    {
        std::destroy_n( this->data(), this->size() );
        this->storage_free();
    }

    constexpr explicit small_vector( size_type const initial_size ) : small_vector( initial_size, default_init ) {}
    constexpr small_vector( size_type const initial_size, no_init_t ) : ptr_base{}
    {
        ptr_base::reset_to_small( N );
        this->storage_init( initial_size );
    }
    constexpr small_vector( size_type const initial_size, default_init_t ) : ptr_base{}
    {
        ptr_base::reset_to_small( N );
        auto const dst{ this->storage_init( initial_size ) };
        std::uninitialized_default_construct_n( dst, initial_size );
    }
    constexpr small_vector( size_type const initial_size, value_init_t ) : ptr_base{}
    {
        ptr_base::reset_to_small( N );
        auto const dst{ this->storage_init( initial_size ) };
        std::uninitialized_value_construct_n( dst, initial_size );
    }
    constexpr small_vector( size_type const count, value_type const & value ) : ptr_base{}
    {
        ptr_base::reset_to_small( N );
        auto const dst{ this->storage_init( count ) };
        std::uninitialized_fill_n( dst, count, value );
    }
    template <std::input_iterator It>
    constexpr small_vector( It const first, It const last ) : ptr_base{}
    {
        ptr_base::reset_to_small( N );
        if constexpr ( std::random_access_iterator<It> )
        {
            auto const sz{ static_cast<size_type>( std::distance( first, last ) ) };
            auto const dst{ this->storage_init( sz ) };
            std::uninitialized_copy_n( first, sz, dst );
        }
        else
        {
            this->storage_init( 0 );
            std::copy( first, last, std::back_inserter( *this ) );
        }
    }
    constexpr small_vector( std::initializer_list<value_type> const init ) : ptr_base{}
    {
        ptr_base::reset_to_small( N );
        auto const sz{ static_cast<size_type>( init.size() ) };
        auto const dst{ this->storage_init( sz ) };
        std::uninitialized_copy_n( init.begin(), sz, dst );
    }

private:
    noninitialized_array<T, N> buffer_;
}; // class small_vector (pointer_based)


////////////////////////////////////////////////////////////////////////////////
// Layout C: Compact LSB (size-first, LSB flag) — trivially relocatable
//
// size_ comes first (before the union), LSB of size_ is the heap discriminant.
// On LE systems the flag is in byte 0 for optimal addressing.
// size() = size_ >> 1, is_heap() = size_ & 1.
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, small_vector_options options>
requires( resolve_layout<T, sz_t>( options.layout ) == small_vector_layout::compact_lsb && is_trivially_moveable<T> )
class [[ nodiscard, clang::trivial_abi ]] small_vector<T, N, sz_t, options>
    :
    public sv_union_base<T, N, sz_t, options>
{
    using mixin = sv_union_base<T, N, sz_t, options>;
    friend mixin;

    static sz_t constexpr max_size_val{ std::numeric_limits<sz_t>::max() >> 1 };

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
    [[ nodiscard, gnu::pure ]] bool is_heap() const noexcept { return ( size_ & 1 ) != 0; }

    void set_inline_size( sz_t const sz ) noexcept
    {
        BOOST_ASSUME( sz <= N );
        size_ = sz << 1; // LSB = 0 → inline
    }
    void set_heap_state( T * __restrict const p, sz_t const cap, sz_t const sz ) noexcept
    {
        BOOST_ASSUME( sz <= max_size_val );
        storage_.heap_.data_     = p;
        storage_.heap_.capacity_ = cap;
        size_                    = ( sz << 1 ) | 1; // LSB = 1 → heap
    }
    void set_size_preserving_flag( sz_t const sz ) noexcept
    {
        BOOST_ASSUME( sz <= max_size_val );
        size_ = ( sz << 1 ) | ( size_ & 1 );
    }

    void do_dec_size() noexcept { BOOST_ASSUME( size() >= 1 ); size_ -= 2; }
    void do_inc_size() noexcept { BOOST_ASSUME( size() < this->capacity() ); size_ += 2; }

    [[ nodiscard, gnu::pure ]] T       * buffer_data()       noexcept { return storage_.buffer_.data; }
    [[ nodiscard, gnu::pure ]] T const * buffer_data() const noexcept { return storage_.buffer_.data; }

    [[ nodiscard, gnu::pure ]] T * __restrict & heap_data_ref()       noexcept { return storage_.heap_.data_; }
    [[ nodiscard, gnu::pure ]] T * __restrict const & heap_data_ref() const noexcept { return storage_.heap_.data_; }

    [[ nodiscard, gnu::pure ]] sz_t       & heap_cap_ref()       noexcept { return storage_.heap_.capacity_; }
    [[ nodiscard, gnu::pure ]] sz_t const & heap_cap_ref() const noexcept { return storage_.heap_.capacity_; }

public:
    using mixin::mixin;

    constexpr small_vector() noexcept : size_{ 0 } {}

    constexpr small_vector( small_vector const & other ) : mixin{} { this->copy_init_from( other ); }
    constexpr small_vector( small_vector && other ) noexcept : mixin{} { this->move_init_from( std::move( other ) ); }
    constexpr small_vector & operator=( small_vector const & other ) { return ( *this = small_vector( other ) ); }
    constexpr small_vector & operator=( small_vector && other ) noexcept { this->move_assign_from( std::move( other ) ); return *this; }
    constexpr small_vector & operator=( std::initializer_list<T> const data ) { this->assign( data ); return *this; }
    constexpr ~small_vector() noexcept { this->destroy_and_free(); }

    [[ nodiscard, gnu::pure ]] sz_t size() const noexcept
    {
        auto const sz{ size_ >> 1 };
        BOOST_ASSUME( sz <= max_size_val );
        return sz;
    }

private:
    sz_t   size_;    // LSB = heap flag, actual size = size_ >> 1
    data_t storage_;
}; // class small_vector (compact_lsb)


////////////////////////////////////////////////////////////////////////////////
// Layout D: Embedded (size inside union, LSB flag) — trivially relocatable
//
// Both union variants start with sz_t at offset 0 (common initial sequence).
// size() = sz_ >> 1, is_heap() = sz_ & 1. Branch-free regardless of T/sz_t.
// No external size_ field — saves sizeof(sz_t) + padding vs compact_lsb when
// the inline side has alignment slack.
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, small_vector_options options>
requires( resolve_layout<T, sz_t>( options.layout ) == small_vector_layout::embedded && is_trivially_moveable<T> )
class [[ nodiscard, clang::trivial_abi ]] small_vector<T, N, sz_t, options>
    :
    public sv_union_base<T, N, sz_t, options>
{
    using mixin = sv_union_base<T, N, sz_t, options>;
    friend mixin;

    static sz_t constexpr max_size_val{ std::numeric_limits<sz_t>::max() >> 1 };

    union data_t {
        // Named struct with no-op ctor — avoids overwriting heap state when
        // inherited constructors re-default-initialize data members after the
        // base-class body.
        struct inline_t {
            sz_t                       sz_;       // LSB=0, actual size = sz_ >> 1
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

    // --- policy interface ---
    // Common initial sequence: heap_.sz_ is always valid to read regardless of
    // active member ([class.union]/7, [class.mem]/25).
    [[ nodiscard, gnu::pure ]] bool is_heap() const noexcept { return ( storage_.heap_.sz_ & 1 ) != 0; }

    void set_inline_size( sz_t const sz ) noexcept
    {
        BOOST_ASSUME( sz <= N );
        storage_.inline_.sz_ = sz << 1; // LSB = 0 → inline
    }
    void set_heap_state( T * __restrict const p, sz_t const cap, sz_t const sz ) noexcept
    {
        BOOST_ASSUME( sz <= max_size_val );
        storage_.heap_.sz_   = ( sz << 1 ) | 1; // LSB = 1 → heap
        storage_.heap_.cap_  = cap;
        storage_.heap_.data_ = p;
    }
    void set_size_preserving_flag( sz_t const sz ) noexcept
    {
        BOOST_ASSUME( sz <= max_size_val );
        storage_.heap_.sz_ = ( sz << 1 ) | ( storage_.heap_.sz_ & 1 );
    }

    void do_dec_size() noexcept { BOOST_ASSUME( size() >= 1 ); storage_.heap_.sz_ -= 2; }
    void do_inc_size() noexcept { BOOST_ASSUME( size() < this->capacity() ); storage_.heap_.sz_ += 2; }

    [[ nodiscard, gnu::pure ]] T       * buffer_data()       noexcept { return storage_.inline_.elements_.data; }
    [[ nodiscard, gnu::pure ]] T const * buffer_data() const noexcept { return storage_.inline_.elements_.data; }

    [[ nodiscard, gnu::pure ]] T * __restrict & heap_data_ref()       noexcept { return storage_.heap_.data_; }
    [[ nodiscard, gnu::pure ]] T * __restrict const & heap_data_ref() const noexcept { return storage_.heap_.data_; }

    [[ nodiscard, gnu::pure ]] sz_t       & heap_cap_ref()       noexcept { return storage_.heap_.cap_; }
    [[ nodiscard, gnu::pure ]] sz_t const & heap_cap_ref() const noexcept { return storage_.heap_.cap_; }

public:
    using mixin::mixin;

    constexpr small_vector() noexcept { storage_.inline_.sz_ = 0; }

    constexpr small_vector( small_vector const & other ) : mixin{} { this->copy_init_from( other ); }
    constexpr small_vector( small_vector && other ) noexcept : mixin{} { this->move_init_from( std::move( other ) ); }
    constexpr small_vector & operator=( small_vector const & other ) { return ( *this = small_vector( other ) ); }
    constexpr small_vector & operator=( small_vector && other ) noexcept { this->move_assign_from( std::move( other ) ); return *this; }
    constexpr small_vector & operator=( std::initializer_list<T> const data ) { this->assign( data ); return *this; }
    constexpr ~small_vector() noexcept { this->destroy_and_free(); }

    // Branch-free: common initial sequence guarantees heap_.sz_ is always the
    // correct sz_t regardless of active union member.
    [[ nodiscard, gnu::pure ]] sz_t size() const noexcept
    {
        auto const sz{ storage_.heap_.sz_ >> 1 };
        BOOST_ASSUME( sz <= max_size_val );
        return sz;
    }

private:
    data_t storage_; // sole data member — no external size_
}; // class small_vector (embedded)


////////////////////////////////////////////////////////////////////////////////
// is_trivially_moveable specializations
////////////////////////////////////////////////////////////////////////////////

// All union-based layouts (compact, compact_lsb, embedded) — trivially
// relocatable iff T is.
template <typename T, std::uint32_t N, typename sz_t, small_vector_options opts>
requires( resolve_layout<T, sz_t>( opts.layout ) != small_vector_layout::pointer_based )
bool constexpr is_trivially_moveable<small_vector<T, N, sz_t, opts>>{ is_trivially_moveable<T> };

// pointer_based is never trivially relocatable (self-referential data_ pointer).
template <typename T, std::uint32_t N, typename sz_t, small_vector_options opts>
requires( resolve_layout<T, sz_t>( opts.layout ) == small_vector_layout::pointer_based )
bool constexpr is_trivially_moveable<small_vector<T, N, sz_t, opts>>{ false };


////////////////////////////////////////////////////////////////////////////////
// C++20-style free-function erasure (ADL)
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, small_vector_options opts, typename Pred>
constexpr typename small_vector<T, N, sz_t, opts>::size_type
erase_if( small_vector<T, N, sz_t, opts> & c, Pred pred )
{
    auto const it{ std::remove_if( c.begin(), c.end(), std::move( pred ) ) };
    auto const n { static_cast<typename small_vector<T, N, sz_t, opts>::size_type>( c.end() - it ) };
    c.shrink_by( n );
    return n;
}

template <typename T, std::uint32_t N, typename sz_t, small_vector_options opts, typename U>
constexpr typename small_vector<T, N, sz_t, opts>::size_type
erase( small_vector<T, N, sz_t, opts> & c, U const & value )
{
    return erase_if( c, [&value]( auto const & elem ) { return elem == value; } );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
