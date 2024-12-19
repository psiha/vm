////////////////////////////////////////////////////////////////////////////////
/// Zero bloat implementation of a classic std::vector around the CRT and/or OS
/// allocation APIs designed for trivially moveable types (eliminating the copy-
/// on-resize overhead of std::vector) + vector_impl extensions.
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

#include <psi/vm/containers/vector_impl.hpp>

#include <boost/assert.hpp>
#include <boost/container/detail/allocation_type.hpp>
#include <boost/container/detail/version_type.hpp>

#include <cstdlib>

#ifdef __linux__
#include <malloc.h>
#elif defined( __APPLE__ )
#include <malloc/malloc.h>
#endif
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace detail
{
    // https://www.gnu.org/software/libc/manual/html_node/Aligned-Memory-Blocks.html
    inline std::uint8_t constexpr guaranteed_alignment{ 16 }; // all known x64 and arm64 platforms

    [[ gnu::pure ]] inline std::size_t crt_alloc_size( void const * const address ) noexcept
    {
        // https://lemire.me/blog/2017/09/15/how-fast-are-malloc_size-and-malloc_usable_size-in-c
#   if defined( _MSC_VER )
        // https://masm32.com/board/index.php?topic=7018.0 HeapAlloc vs HeapReAlloc
        return _msize( const_cast<void *>( address ) ); // uber slow & almost useless as it returns the requested alloc size, not the capacity of the allocated block (it just calls HeapSize)
#   elif defined( __linux__ )
        return ::malloc_usable_size( const_cast<void *>( address ) ); // fast
#   elif defined( __APPLE__ )
        return ::malloc_size( address ); // not so fast
#   else
        static_assert( false, "no malloc size implementation" );
#   endif
    }
    template <std::uint8_t alignment>
    std::size_t crt_aligned_alloc_size( void const * const address ) noexcept
    {
#   if defined( _MSC_VER )
        if constexpr ( alignment > guaranteed_alignment )
            return _aligned_msize( const_cast<void *>( address ), alignment, 0 );
#   endif
        return crt_alloc_size( address );
    }

    [[ using gnu: assume_aligned( guaranteed_alignment ), malloc, returns_nonnull ]]
#ifdef _MSC_VER
    __declspec( noalias, restrict )
#endif
    inline void * crt_realloc( void * const existing_allocation_address, std::size_t const new_size )
    {
        auto const new_allocation{ std::realloc( existing_allocation_address, new_size ) };
        if ( !new_allocation ) [[ unlikely ]]
            throw_bad_alloc();
        return new_allocation;
    }

    [[ using gnu: assume_aligned( guaranteed_alignment ), malloc, returns_nonnull ]]
#ifdef _MSC_VER
    __declspec( noalias, restrict )
#endif
    inline void * crt_aligned_realloc( void * const existing_allocation_address, std::size_t const existing_allocation_size, std::size_t const new_size, std::uint8_t const alignment )
    {
        BOOST_ASSUME( alignment > guaranteed_alignment );
#   if defined( _MSC_VER )
        std::ignore = existing_allocation_size;
        auto const new_allocation{ ::_aligned_realloc( existing_allocation_address, new_size, alignment ) };
#   else
        void * new_allocation{ nullptr };
        if ( existing_allocation_address ) [[ likely ]]
        {
            auto const try_realloc{ std::realloc( existing_allocation_address, new_size ) };
            if ( is_aligned( try_realloc, alignment ) ) [[ likely ]]
            {
                new_allocation = try_realloc;
            }
            else
            {
                BOOST_ASSUME( try_realloc ); // nullptr handled implicitly above
                if ( posix_memalign( &new_allocation, alignment, new_size ) == 0 ) [[ likely ]]
                    std::memcpy( new_allocation, try_realloc, existing_allocation_size );
                std::free( try_realloc );
            }
        }
        else
        {
            BOOST_ASSUME( !existing_allocation_size );
            // "On Linux (and other systems), posix_memalign() does not modify
            // memptr on failure. A requirement standardizing this behavior was
            // added in POSIX .1 - 2008 TC2."
            BOOST_VERIFY( posix_memalign( &new_allocation, alignment, new_size ) == 0 );
        }
#   endif
        if ( !new_allocation ) [[ unlikely ]]
            throw_bad_alloc();
        return new_allocation;
    } // crt_aligned_realloc()

    template <std::uint8_t alignment>
    [[ using gnu: assume_aligned( alignment ), malloc, returns_nonnull ]]
#ifdef _MSC_VER
    __declspec( noalias, restrict )
#endif
    void * crt_realloc( void * const existing_allocation_address, std::size_t const existing_allocation_size, std::size_t const new_size )
    {
        if constexpr ( alignment > guaranteed_alignment )
            return crt_aligned_realloc( existing_allocation_address, existing_allocation_size, new_size, alignment );
        else
            return crt_realloc( existing_allocation_address, new_size );
    }
#ifdef _MSC_VER
    __declspec( noalias )
#endif
    inline
    void crt_aligned_free( void * const allocation ) noexcept
    {
#   ifdef __clang__
        if ( __builtin_constant_p( allocation ) && !allocation )
            return;
#   endif
#   if defined( _MSC_VER )
        ::_aligned_free( allocation );
#   else
        std::free( allocation );
#   endif
    }

    template <typename T, bool> struct capacity           { T value; };
    template <typename T      > struct capacity<T, false> {
        constexpr capacity() = default;
        constexpr capacity( std::nullptr_t ) noexcept {}
    };
} // namespace detail

template <typename T, typename sz_t = std::size_t, std::uint8_t alignment = alignof( T )>
struct crt_aligned_allocator
{
    using value_type      = T;
    using       pointer   = T *;
    using const_pointer   = T const *;
    using       reference = T &;
    using const_reference = T const &;
    using       size_type = sz_t;
    using difference_type = std::make_signed_t<size_type>;

    using allocation_commands = std::uint8_t;
    using version = boost::container::dtl::version_type<crt_aligned_allocator, 2>;

    template <class U> struct rebind { using other = crt_aligned_allocator<U, sz_t, alignment>; };

    //!Allocates memory for an array of count elements.
    //!Throws bad_alloc if there is no enough memory
    //!If Version is 2, this allocated memory can only be deallocated
    //!with deallocate() or (for Version == 2) deallocate_many()
    [[ nodiscard ]]
    [[ using gnu: cold, assume_aligned( alignment ), malloc, returns_nonnull ]]
#ifdef _MSC_VER
    __declspec( noalias, restrict )
#endif
    static pointer allocate( size_type const count, [[ maybe_unused ]] void const * const hint = nullptr )
    {
        BOOST_ASSUME( count < max_size() );
        auto const byte_size{ count * sizeof( T ) };
        void * new_allocation{ nullptr };
        if constexpr ( alignment > detail::guaranteed_alignment )
        {
#       if defined( _MSC_VER )
            new_allocation = ::_aligned_malloc( byte_size, alignment );
#       else
            BOOST_VERIFY( posix_memalign( &new_allocation, alignment, byte_size ) == 0 );
#       endif
        }
        else
        {
            new_allocation = std::malloc( byte_size );
        }

        if ( !new_allocation ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return std::assume_aligned<alignment>( static_cast<pointer>( new_allocation ) );
    }

    static auto allocate_at_least( size_type const count )
    {
        auto const ptr{ allocate( count ) };
        return std::allocation_result{ ptr, size( ptr ) };
    }

    //!Deallocates previously allocated memory.
    //!Never throws
    static void deallocate( pointer const ptr, [[ maybe_unused ]] size_type const size ) noexcept
    {
#   ifdef __clang__
        if ( __builtin_constant_p( ptr ) && !ptr )
            return;
#   endif
        if constexpr ( alignment > detail::guaranteed_alignment )
            return detail::crt_aligned_free( ptr );
        else
            return std::free( ptr );
    }

    [[ nodiscard ]] static pointer resize( pointer const current_address, size_type const current_size, size_type const target_size )
    {
        auto const result_address{ do_resize( current_address, current_size, target_size ) };
        if ( !result_address ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return result_address;
    }
    [[ nodiscard ]] static pointer grow_to( pointer const current_address, size_type const current_size, size_type const target_size )
    {
        BOOST_ASSUME( target_size >= current_size );
        return resize( current_address, current_size, target_size );
    }
    [[ nodiscard ]] static pointer shrink_to( pointer const current_address, size_type const current_size, size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= current_size );
        return do_resize( current_address, current_size, target_size );
    }

    //!Returns the maximum number of elements that could be allocated.
    //!Never throws
    [[ gnu::const ]] static constexpr size_type max_size() noexcept { return std::numeric_limits<size_type>::max() / sizeof( T ); }

    //!An advanced function that offers in-place expansion shrink to fit and new allocation
    //!capabilities. Memory allocated with this function can only be deallocated with deallocate()
    //!or deallocate_many().
    // https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2006/n2045.html
    [[ nodiscard ]] static pointer allocation_command
    (
        allocation_commands const command,
        [[ maybe_unused ]]
        size_type           const limit_size,
        size_type & prefer_in_recvd_out_size,
        pointer   & reuse
    )
    {
        namespace bc = boost::container;

        BOOST_ASSERT_MSG( !( command & bc::zero_memory ), "Unimplemented command" );
        BOOST_ASSERT_MSG( !!( command & bc::shrink_in_place ) != !!( command & ( bc::allocate_new | bc::expand_fwd | bc::expand_bwd ) ), "Conflicting commands" );

        auto const preferred_size     { prefer_in_recvd_out_size };
        auto const preferred_byte_size{ static_cast<size_type>( preferred_size * sizeof( T ) ) };
        auto const current_size       { reuse ? size( reuse ) : 0 };
        bool success{ false };
#   ifdef _MSC_VER  // no try_realloc for others so this cannot be safely implemented
        // TODO:
        //  - Linux: switch to mmap+mremap on for non trivial_abi types
        //  - OSX https://stackoverflow.com/questions/72637850/how-to-use-virtual-memory-implement-realloc-on-mac-osx
        if ( reuse && ( command & bc::expand_fwd ) && ( alignment <= detail::guaranteed_alignment ) )
        {
            BOOST_ASSUME( preferred_size >= current_size );
            auto const expand_result{ ::_expand( reuse, preferred_byte_size ) };
            if ( expand_result )
            {
                BOOST_ASSUME( reuse == expand_result );
                success = true;
            }
        }
        else
#   endif
        if ( reuse && ( command & ( bc::shrink_in_place | bc::try_shrink_in_place ) ) )
        {
            BOOST_ASSUME( preferred_size <= current_size );
            auto const new_address{ std::realloc( reuse, preferred_byte_size ) };
            BOOST_ASSUME( new_address == reuse );
            reuse   = new_address;
            success = true;
        }
        else
        if ( command & bc::allocate_new )
        {
            reuse   = allocate( preferred_size );
            success = true; // allocate() throws
        }
        else
        {
            std::unreachable();
        }

        if ( success ) [[ likely ]]
        {
            BOOST_ASSUME( reuse );
            prefer_in_recvd_out_size = size( reuse );
            return reuse;
        }

        if ( !( command & bc::nothrow_allocation ) )
            detail::throw_bad_alloc();

        return nullptr;
    }

    //!Returns the maximum number of objects the previously allocated memory
    //!pointed by p can hold.
    [[ nodiscard, gnu::pure ]] static size_type size( const_pointer const p ) noexcept
    {
        return static_cast<size_type>( detail::crt_aligned_alloc_size<alignment>( p ) / sizeof( T ) );
    }

    //!Allocates just one object. Memory allocated with this function
    //!must be deallocated only with deallocate_one().
    //!Throws bad_alloc if there is no enough memory
    [[ nodiscard ]] static pointer allocate_one() { return allocate( 1 ); }
    //!Deallocates memory previously allocated with allocate_one().
    //!You should never use deallocate_one to deallocate memory allocated
    //!with other functions different from allocate_one() or allocate_individual.
    //Never throws
    static void deallocate_one( pointer const p ) noexcept { return deallocate( p, 1 ); }

private:
    [[ gnu::cold, gnu::assume_aligned( alignment ) ]]
    [[ nodiscard ]] static pointer do_resize( pointer const existing_allocation_address, size_type const existing_allocation_size, size_type const new_size )
    {
        return std::assume_aligned<alignment>( static_cast<pointer>(
            detail::crt_realloc<alignment>( existing_allocation_address, existing_allocation_size * sizeof( T ), new_size * sizeof( T ) )
        ));
    }
#if 0 // not implemented/supported/needed yet
    using void_multiallocation_chain = boost::container::dtl::basic_multiallocation_chain<void *>;
    using      multiallocation_chain = boost::container::dtl::transform_multiallocation_chain<void_multiallocation_chain, T>;

    //!Allocates many elements of size == 1.
    //!Elements must be individually deallocated with deallocate_one()
    void allocate_individual( std::size_t num_elements, multiallocation_chain & chain ) = delete;

    //!Deallocates memory allocated with allocate_one() or allocate_individual().
    //!This function is available only with Version == 2
    void deallocate_individual( multiallocation_chain & chain ) noexcept = delete;

    //!Allocates many elements of size elem_size.
    //!Elements must be individually deallocated with deallocate()
    void allocate_many( size_type elem_size, std::size_t n_elements, multiallocation_chain & chain ) = delete;

    //!Allocates n_elements elements, each one of size elem_sizes[i]
    //!Elements must be individually deallocated with deallocate()
    void allocate_many( const size_type * elem_sizes, size_type n_elements, multiallocation_chain & chain ) = delete;

    //!Deallocates several elements allocated by
    //!allocate_many(), allocate(), or allocation_command().
    void deallocate_many( multiallocation_chain & chain ) noexcept = delete;
#endif
}; // class crt_aligned_allocator


struct crt_vector_options
{
    std::uint8_t alignment                { 0    }; // 0 -> default
    bool         cache_capacity           { true }; // if your crt_alloc_size is slow
    bool         explicit_geometric_growth{ true }; // if your realloc impl is slow (yes MSVC we are again looking at you)
}; // struct crt_vector_options


template <typename T, typename sz_t = std::size_t, crt_vector_options options = {}>
requires( is_trivially_moveable<T> )
class [[ nodiscard, clang::trivial_abi ]] crt_vector
    :
    public vector_impl<crt_vector<T, sz_t, options>, T, sz_t>
{
public:
    static std::uint8_t constexpr alignment{ options.alignment ? options.alignment : std::uint8_t{ alignof( T ) } };

    using size_type      = sz_t;
    using value_type     = T;
    using allocator_type = crt_aligned_allocator<T, sz_t, alignment>;

private:
    using al   = allocator_type;
    using base = vector_impl<crt_vector<T, sz_t, options>, T, sz_t>;

public:
    using base::base;
    constexpr crt_vector() noexcept : p_array_{ nullptr }, size_{ 0 }, capacity_{ 0 } {}
    constexpr explicit crt_vector( crt_vector const & other )
    {
        auto const data{ storage_init( other.size() ) };
        try { std::uninitialized_copy_n( other.data(), other.size(), data ); }
        catch(...) { al::deallocate( data, capacity() ); throw; }
    }
    constexpr crt_vector( crt_vector && other ) noexcept : p_array_{ other.p_array_ }, size_{ other.size_ }, capacity_{ other.capacity_ } { other.mark_freed(); }

    constexpr crt_vector & operator=( crt_vector const & other ) { *this = crt_vector( other ); }
    constexpr crt_vector & operator=( crt_vector && other ) noexcept( std::is_nothrow_move_constructible_v<T> )
    {
        std::swap( this->p_array_ , other.p_array_  );
        std::swap( this->size_    , other.size_     );
        std::swap( this->capacity_, other.capacity_ );
        other.free();
        return *this;
    }
    constexpr ~crt_vector() noexcept { free(); }

    [[ nodiscard, gnu::pure ]] size_type size    () const noexcept { return size_; }
    [[ nodiscard, gnu::pure ]] size_type capacity() const noexcept
    {
        if constexpr ( options.cache_capacity )
        {
            BOOST_ASSUME( capacity_.value >= size_ );
            BOOST_ASSERT( this->empty() || ( capacity_.value <= al::size( p_array_ ) ) );
            return capacity_.value;
        }
        else
        {
            return this->empty() ? 0 : al::size( p_array_ );
        }
    }

    [[ nodiscard, gnu::pure, gnu::assume_aligned( alignment ) ]] value_type       * data()       noexcept { return std::assume_aligned<alignment>( p_array_ ); }
    [[ nodiscard, gnu::pure, gnu::assume_aligned( alignment ) ]] value_type const * data() const noexcept { return std::assume_aligned<alignment>( p_array_ ); }

    void reserve( size_type const new_capacity )
    {
        auto const quick_comparison_target{ options.cache_capacity ? capacity() : size() };
        if ( new_capacity > quick_comparison_target ) {
            p_array_ = al::grow_to( p_array_, quick_comparison_target, new_capacity );
            update_capacity( new_capacity );
        }
    }

    static constexpr al get_allocator() noexcept { return {}; }

private: friend base;
    [[ using gnu: cold, assume_aligned( alignment ), malloc, returns_nonnull, noinline ]]
#ifdef _MSC_VER
    __declspec( noalias, restrict )
#endif
    constexpr value_type * storage_init( size_type const initial_size )
    {
        p_array_ = al::allocate( initial_size );
        size_    = initial_size;
        update_capacity( initial_size );
        return data();
    }
    [[ using gnu: assume_aligned( alignment ), returns_nonnull ]]
    constexpr value_type * storage_grow_to( size_type const target_size )
    {
        auto const current_capacity{ capacity() };
        BOOST_ASSUME( current_capacity >= size_ );
        BOOST_ASSUME( target_size      >  size_ );
        if ( target_size > current_capacity ) [[ unlikely ]] {
            do_grow( target_size, current_capacity );
        }
        size_ = target_size;
        return data();
    }

    [[ using gnu: assume_aligned( alignment ), returns_nonnull, cold ]]
    constexpr value_type * storage_shrink_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= size_ );
        p_array_ = al::shrink_to( p_array_, size_, target_size );
        BOOST_ASSUME( p_array_ ); // assuming downsizing never fails
        BOOST_ASSUME( is_aligned( p_array_, alignment ) ); // assuming no implementation will actually move a block upon downsizing
        storage_shrink_size_to( target_size );
        update_capacity( target_size );
        return data();
    }
    constexpr void storage_shrink_size_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( size_ >= target_size );
        size_ = target_size;
    }
    void storage_dec_size() noexcept { BOOST_ASSUME( size_ >= 1 ); --size_; }
    void storage_inc_size() noexcept; // TODO

private:
    [[ gnu::cold, gnu::noinline, clang::preserve_most ]]
    void do_grow( size_type const target_size, size_type const cached_current_capacity )
    {
        BOOST_ASSUME( cached_current_capacity == capacity() );
        auto const new_capacity
        {
            options.explicit_geometric_growth
                ? std::max( target_size, cached_current_capacity * 3U / 2U )
                : target_size
        };
        p_array_ = al::grow_to( p_array_, cached_current_capacity, new_capacity );
        update_capacity( new_capacity );
    }

    void update_capacity( [[ maybe_unused ]] size_type const requested_capacity ) noexcept
    {
        BOOST_ASSUME( p_array_ );
        if constexpr ( options.cache_capacity ) {
#       if defined( _MSC_VER )
            BOOST_ASSERT( al::size( p_array_ ) == requested_capacity ); // see note in crt_alloc_size
            capacity_.value = requested_capacity;
#       else
            capacity_.value = al::size( p_array_ );
            BOOST_ASSUME( capacity_.value >= requested_capacity );
#       endif
        }
    }

    void free() noexcept
    {
        std::destroy_n( data(), size() );
        al::deallocate( data(), capacity() );
        mark_freed();
    }

    void mark_freed() noexcept { std::memset( this, 0, sizeof( *this ) ); }

private:
    T * __restrict p_array_;
    size_type      size_;
#ifdef _MSC_VER
    [[ msvc::no_unique_address ]]
#else
    [[ no_unique_address ]]
#endif
    detail::capacity<sz_t, options.cache_capacity> capacity_;
}; // struct crt_vector

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
