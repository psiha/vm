////////////////////////////////////////////////////////////////////////////////
/// Small vector — inline (stack) buffer with heap spill
///
/// Three layout modes selectable via NTTP options:
///  - compact (default): union-based, MSB-of-size flag, trivially relocatable,
///    [[clang::trivial_abi]]. No self-referential pointer.
///  - compact_lsb: like compact but size_ comes first (before the union) and
///    LSB of size_ is the heap flag. On LE systems the flag is in byte 0 of the
///    struct for optimal addressing (test byte [ptr], 1).
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
    compact_lsb,   // union-based, size-first with LSB flag, trivially relocatable
    compact,       // union-based, MSB-of-size flag, trivially relocatable
    pointer_based  // LLVM/Boost style, type-erasable
};

struct small_vector_options
{
    std::uint8_t        alignment{ 0 };
    geometric_growth    growth   { .num = 5, .den = 4 }; // 1.25x default (conservative for small vectors)
    small_vector_layout layout   { /*first as default*/ };
}; // struct small_vector_options


////////////////////////////////////////////////////////////////////////////////
// Forward declarations
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t = std::size_t, small_vector_options options = {}>
class small_vector;

// pointer_based layout base (N-independent)
template <typename T, typename sz_t, small_vector_options options>
requires( options.layout == small_vector_layout::pointer_based && is_trivially_moveable<T> )
class small_vector_base;


////////////////////////////////////////////////////////////////////////////////
// Layout A: Compact (union-based) — trivially relocatable
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, small_vector_options options>
requires( options.layout == small_vector_layout::compact && is_trivially_moveable<T> )
class [[ nodiscard, clang::trivial_abi ]] small_vector<T, N, sz_t, options>
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
    using al   = allocator_type;
    using base = vector_impl<small_vector, T, sz_t>;

    static sz_t constexpr heap_flag   { sz_t{ 1 } << ( sizeof( sz_t ) * CHAR_BIT - 1 ) };
    static sz_t constexpr size_mask   { ~heap_flag };
    static sz_t constexpr max_size_val{ size_mask  };

    // Named union with explicit ctor/dtor so that the noninitialized_array
    // variant member does not delete the enclosing type's default constructor.
    union data_t {
        constexpr  data_t() noexcept : buffer_{} {}
        constexpr ~data_t() noexcept {}

        struct {
            T * __restrict data_;
            sz_t           capacity_;
        } heap_;
        noninitialized_array<T, N> buffer_;
    };

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

public:
    using base::base;

    constexpr small_vector() noexcept : size_{ 0 } {}

    constexpr small_vector( small_vector const & other ) : base{}
    {
        auto const sz{ other.size() };
        auto const dst{ storage_init( sz ) };
        try { std::uninitialized_copy_n( other.data(), sz, dst ); }
        catch( ... ) { storage_free(); throw; }
    }

    constexpr small_vector( small_vector && other ) noexcept : base{}
    {
        auto const sz{ other.size() };
        if ( other.is_heap() )
        {
            set_heap_state( other.storage_.heap_.data_, other.storage_.heap_.capacity_, sz );
        }
        else
        {
            std::memcpy( static_cast<void *>( storage_.buffer_.data ), other.storage_.buffer_.data, sz * sizeof( T ) );
            set_inline_size( sz );
        }
        other.set_inline_size( 0 );
    }

    constexpr small_vector & operator=( small_vector const & other )
    {
        return ( *this = small_vector( other ) );
    }

    constexpr small_vector & operator=( small_vector && other ) noexcept
    {
        this->clear();
        if ( is_heap() )
            al::deallocate( storage_.heap_.data_, storage_.heap_.capacity_ );
        auto const sz{ other.size() };
        if ( other.is_heap() )
        {
            set_heap_state( other.storage_.heap_.data_, other.storage_.heap_.capacity_, sz );
        }
        else
        {
            std::memcpy( static_cast<void *>( storage_.buffer_.data ), other.storage_.buffer_.data, sz * sizeof( T ) );
            set_inline_size( sz );
        }
        other.set_inline_size( 0 );
        return *this;
    }

    constexpr small_vector & operator=( std::initializer_list<value_type> const data ) { this->assign( data ); return *this; }

    constexpr ~small_vector() noexcept
    {
        std::destroy_n( data(), size() );
        if ( is_heap() )
            al::deallocate( storage_.heap_.data_, storage_.heap_.capacity_ );
    }

    [[ nodiscard, gnu::pure ]] sz_t size() const noexcept
    {
        auto const sz{ size_ & size_mask };
        BOOST_ASSUME( sz <= max_size_val );
        return sz;
    }
    [[ nodiscard, gnu::pure ]] sz_t capacity() const noexcept
    {
        return is_heap() ? storage_.heap_.capacity_ : sz_t{ N };
    }

    [[ nodiscard, gnu::pure ]] value_type       * data()       noexcept { return is_heap() ? storage_.heap_.data_ : storage_.buffer_.data; }
    [[ nodiscard, gnu::pure ]] value_type const * data() const noexcept { return is_heap() ? storage_.heap_.data_ : storage_.buffer_.data; }

    void reserve( size_type const new_capacity )
    {
        if ( new_capacity > capacity() )
            grow_heap( new_capacity );
    }

private: friend base;
    [[ gnu::cold ]]
#ifdef _MSC_VER
    __declspec( noalias )
#endif
    constexpr value_type * storage_init( size_type const initial_size )
    {
        if ( initial_size > N )
        {
            auto const p{ al::allocate( initial_size ) };
            set_heap_state( p, initial_size, initial_size );
            return p;
        }
        set_inline_size( initial_size );
        return storage_.buffer_.data;
    }

    [[ gnu::returns_nonnull ]]
    constexpr value_type * storage_grow_to( size_type const target_size )
    {
        auto const current_cap{ capacity() };
        BOOST_ASSUME( target_size >= size() );
        if ( target_size > current_cap ) [[ unlikely ]]
        {
            grow_heap( options.growth( target_size, current_cap ) );
        }
        set_size_preserving_flag( target_size );
        return data();
    }

    constexpr value_type * storage_shrink_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= size() );
        if ( is_heap() )
        {
            storage_.heap_.data_     = al::shrink_to( storage_.heap_.data_, size(), target_size );
            storage_.heap_.capacity_ = target_size;
            BOOST_ASSUME( storage_.heap_.data_ || !target_size );
        }
        set_size_preserving_flag( target_size );
        return data();
    }

    constexpr void storage_shrink_size_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( size() >= target_size );
        set_size_preserving_flag( target_size );
    }

    void storage_dec_size() noexcept
    {
        BOOST_ASSUME( size() >= 1 );
        --size_;
    }
    void storage_inc_size() noexcept
    {
        BOOST_ASSUME( size() < capacity() );
        ++size_;
    }

#ifdef _MSC_VER
    bool storage_try_expand_capacity( size_type const target_capacity ) noexcept
    requires( alignment <= detail::guaranteed_alignment )
    {
        if ( !is_heap() )
            return false;
        namespace bc = boost::container;
        auto recv_size{ target_capacity };
        auto reuse    { storage_.heap_.data_ };
        auto const result{ al::allocation_command(
            bc::expand_fwd | bc::nothrow_allocation,
            target_capacity, recv_size, reuse
        ) };
        if ( result )
        {
            storage_.heap_.capacity_ = recv_size;
            return true;
        }
        return false;
    }
#endif

    void storage_free() noexcept
    {
        if ( is_heap() )
            al::deallocate( storage_.heap_.data_, storage_.heap_.capacity_ );
        set_inline_size( 0 );
    }

private:
    [[ gnu::cold, gnu::noinline, clang::preserve_most ]]
    void grow_heap( size_type const new_capacity )
    {
        BOOST_ASSUME( new_capacity > capacity() );
        if ( is_heap() )
        {
            storage_.heap_.data_     = al::grow_to( storage_.heap_.data_, storage_.heap_.capacity_, new_capacity );
            storage_.heap_.capacity_ = new_capacity;
        }
        else
        {
            auto const sz{ size() };
            auto const p { al::allocate( new_capacity ) };
            std::memcpy( static_cast<void *>( p ), storage_.buffer_.data, sz * sizeof( T ) );
            set_heap_state( p, new_capacity, sz );
        }
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
// Like compact, but size_ comes first (before the union) and the LSB of size_
// is the heap discriminant. On little-endian, this places the flag in byte 0
// of the struct for optimal addressing (test byte [ptr], 1).
// size() = size_ >> 1, is_heap() = size_ & 1.
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, small_vector_options options>
requires( options.layout == small_vector_layout::compact_lsb && is_trivially_moveable<T> )
class [[ nodiscard, clang::trivial_abi ]] small_vector<T, N, sz_t, options>
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
    using al   = allocator_type;
    using base = vector_impl<small_vector, T, sz_t>;

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

public:
    using base::base;

    constexpr small_vector() noexcept : size_{ 0 } {}

    constexpr small_vector( small_vector const & other ) : base{}
    {
        auto const sz{ other.size() };
        auto const dst{ storage_init( sz ) };
        try { std::uninitialized_copy_n( other.data(), sz, dst ); }
        catch( ... ) { storage_free(); throw; }
    }

    constexpr small_vector( small_vector && other ) noexcept : base{}
    {
        auto const sz{ other.size() };
        if ( other.is_heap() )
        {
            set_heap_state( other.storage_.heap_.data_, other.storage_.heap_.capacity_, sz );
        }
        else
        {
            std::memcpy( static_cast<void *>( storage_.buffer_.data ), other.storage_.buffer_.data, sz * sizeof( T ) );
            set_inline_size( sz );
        }
        other.set_inline_size( 0 );
    }

    constexpr small_vector & operator=( small_vector const & other )
    {
        return ( *this = small_vector( other ) );
    }

    constexpr small_vector & operator=( small_vector && other ) noexcept
    {
        this->clear();
        if ( is_heap() )
            al::deallocate( storage_.heap_.data_, storage_.heap_.capacity_ );
        auto const sz{ other.size() };
        if ( other.is_heap() )
        {
            set_heap_state( other.storage_.heap_.data_, other.storage_.heap_.capacity_, sz );
        }
        else
        {
            std::memcpy( static_cast<void *>( storage_.buffer_.data ), other.storage_.buffer_.data, sz * sizeof( T ) );
            set_inline_size( sz );
        }
        other.set_inline_size( 0 );
        return *this;
    }

    constexpr small_vector & operator=( std::initializer_list<value_type> const data ) { this->assign( data ); return *this; }

    constexpr ~small_vector() noexcept
    {
        std::destroy_n( data(), size() );
        if ( is_heap() )
            al::deallocate( storage_.heap_.data_, storage_.heap_.capacity_ );
    }

    [[ nodiscard, gnu::pure ]] sz_t size() const noexcept
    {
        auto const sz{ size_ >> 1 };
        BOOST_ASSUME( sz <= max_size_val );
        return sz;
    }
    [[ nodiscard, gnu::pure ]] sz_t capacity() const noexcept
    {
        return is_heap() ? storage_.heap_.capacity_ : sz_t{ N };
    }

    [[ nodiscard, gnu::pure ]] value_type       * data()       noexcept { return is_heap() ? storage_.heap_.data_ : storage_.buffer_.data; }
    [[ nodiscard, gnu::pure ]] value_type const * data() const noexcept { return is_heap() ? storage_.heap_.data_ : storage_.buffer_.data; }

    void reserve( size_type const new_capacity )
    {
        if ( new_capacity > capacity() )
            grow_heap( new_capacity );
    }

private: friend base;
    [[ gnu::cold ]]
#ifdef _MSC_VER
    __declspec( noalias )
#endif
    constexpr value_type * storage_init( size_type const initial_size )
    {
        if ( initial_size > N )
        {
            auto const p{ al::allocate( initial_size ) };
            set_heap_state( p, initial_size, initial_size );
            return p;
        }
        set_inline_size( initial_size );
        return storage_.buffer_.data;
    }

    [[ gnu::returns_nonnull ]]
    constexpr value_type * storage_grow_to( size_type const target_size )
    {
        auto const current_cap{ capacity() };
        BOOST_ASSUME( target_size >= size() );
        if ( target_size > current_cap ) [[ unlikely ]]
        {
            grow_heap( options.growth( target_size, current_cap ) );
        }
        set_size_preserving_flag( target_size );
        return data();
    }

    constexpr value_type * storage_shrink_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= size() );
        if ( is_heap() )
        {
            storage_.heap_.data_     = al::shrink_to( storage_.heap_.data_, size(), target_size );
            storage_.heap_.capacity_ = target_size;
            BOOST_ASSUME( storage_.heap_.data_ || !target_size );
        }
        set_size_preserving_flag( target_size );
        return data();
    }

    constexpr void storage_shrink_size_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( size() >= target_size );
        set_size_preserving_flag( target_size );
    }

    void storage_dec_size() noexcept
    {
        BOOST_ASSUME( size() >= 1 );
        size_ -= 2; // size is stored << 1, so decrement by 2
    }
    void storage_inc_size() noexcept
    {
        BOOST_ASSUME( size() < capacity() );
        size_ += 2; // size is stored << 1, so increment by 2
    }

#ifdef _MSC_VER
    bool storage_try_expand_capacity( size_type const target_capacity ) noexcept
    requires( alignment <= detail::guaranteed_alignment )
    {
        if ( !is_heap() )
            return false;
        namespace bc = boost::container;
        auto recv_size{ target_capacity };
        auto reuse    { storage_.heap_.data_ };
        auto const result{ al::allocation_command(
            bc::expand_fwd | bc::nothrow_allocation,
            target_capacity, recv_size, reuse
        ) };
        if ( result )
        {
            storage_.heap_.capacity_ = recv_size;
            return true;
        }
        return false;
    }
#endif

    void storage_free() noexcept
    {
        if ( is_heap() )
            al::deallocate( storage_.heap_.data_, storage_.heap_.capacity_ );
        set_inline_size( 0 );
    }

private:
    [[ gnu::cold, gnu::noinline, clang::preserve_most ]]
    void grow_heap( size_type const new_capacity )
    {
        BOOST_ASSUME( new_capacity > capacity() );
        if ( is_heap() )
        {
            storage_.heap_.data_     = al::grow_to( storage_.heap_.data_, storage_.heap_.capacity_, new_capacity );
            storage_.heap_.capacity_ = new_capacity;
        }
        else
        {
            auto const sz{ size() };
            auto const p { al::allocate( new_capacity ) };
            std::memcpy( static_cast<void *>( p ), storage_.buffer_.data, sz * sizeof( T ) );
            set_heap_state( p, new_capacity, sz );
        }
    }

private:
    sz_t   size_;    // LSB = heap flag, actual size = size_ >> 1 (at offset 0 → first byte on LE)
    data_t storage_;
}; // class small_vector (compact_lsb)


////////////////////////////////////////////////////////////////////////////////
// is_trivially_moveable specializations
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t N, typename sz_t, small_vector_options opts>
requires( opts.layout == small_vector_layout::compact )
bool constexpr is_trivially_moveable<small_vector<T, N, sz_t, opts>>{ is_trivially_moveable<T> };

template <typename T, std::uint32_t N, typename sz_t, small_vector_options opts>
requires( opts.layout == small_vector_layout::compact_lsb )
bool constexpr is_trivially_moveable<small_vector<T, N, sz_t, opts>>{ is_trivially_moveable<T> };

template <typename T, std::uint32_t N, typename sz_t, small_vector_options opts>
requires( opts.layout == small_vector_layout::pointer_based )
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
