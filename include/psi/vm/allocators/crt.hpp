////////////////////////////////////////////////////////////////////////////////
///
/// \file crt.hpp
/// --------------
///
/// CRT-based allocator using malloc/realloc/free (or their aligned variants).
///
/// Copyright (c) Domagoj Saric.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once

#include <psi/vm/align.hpp>
#include <psi/vm/allocators/allocator_base.hpp>

#include <boost/assert.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring> // memcpy
#include <memory>  // assume_aligned
#include <tuple>   // std::ignore

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

    // From GCC docs: realloc-like functions have this property (malloc/restrict) as long as the old pointer is never referred to (including comparing it to the new pointer) after the function returns a non-NULL value.
    [[ using gnu: cold, assume_aligned( guaranteed_alignment ), malloc ]]
#ifdef _MSC_VER
    __declspec( restrict, noalias )
#endif
    inline void * crt_realloc( void * const existing_allocation_address, std::size_t const new_size )
    {
        auto const new_allocation{ std::realloc( existing_allocation_address, new_size ) };
        if ( ( new_allocation == nullptr ) && ( new_size != 0 ) ) [[ unlikely ]]
            throw_bad_alloc();
        return new_allocation;
    }

    [[ using gnu: cold, assume_aligned( guaranteed_alignment ), malloc, ]]
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
        if ( ( new_allocation == nullptr ) && ( new_size != 0 ) ) [[ unlikely ]]
            throw_bad_alloc();
        return new_allocation;
    } // crt_aligned_realloc()

    template <std::uint8_t alignment>
    [[ using gnu: assume_aligned( alignment ), malloc ]]
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
#   if __has_builtin( __builtin_constant_p )
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

template <typename T, typename sz_t = std::size_t>
struct crt_allocator
    : allocator_base<T, sz_t>
{
    using base = allocator_base<T, sz_t>;
    using version = base::template version_type<crt_allocator>;
    // Redeclare directly (not 'using typename base::') so MSVC sees raw pointer
    // types for __declspec(restrict) validation.
    using value_type    = T;
    using pointer       = T *;
    using const_pointer = T const *;
    using size_type     = sz_t;

    template <class U> struct rebind { using other = crt_allocator<U, sz_t>; };

    //!Allocates memory for an array of count elements.
    //!Throws bad_alloc if there is no enough memory
    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]]
    [[ using gnu: cold, assume_aligned( alignment ), malloc, returns_nonnull ]]
#ifdef _MSC_VER
    __declspec( restrict, noalias )
#endif
    static pointer allocate( size_type const count, [[ maybe_unused ]] void const * const hint = nullptr )
    {
        BOOST_ASSUME( count < base::max_size() );
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

    //!Deallocates previously allocated memory.
    //!Never throws
    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    static void deallocate( pointer const ptr, [[ maybe_unused ]] size_type const size = 0 ) noexcept
    {
#   if __has_builtin( __builtin_constant_p )
        if ( __builtin_constant_p( ptr ) && !ptr )
            return;
#   endif
        if constexpr ( alignment > detail::guaranteed_alignment )
            return detail::crt_aligned_free( ptr );
        else
            return std::free( ptr );
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]] static pointer grow_to( pointer const current_address, size_type const current_size, size_type const target_size )
    {
        BOOST_ASSUME( target_size >= current_size );
        return do_resize<alignment>( current_address, current_size, target_size );
    }
    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]] static pointer shrink_to( pointer const current_address, size_type const current_size, size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= current_size );
        return do_resize<alignment>( current_address, current_size, target_size );
    }

    //!Returns the maximum number of objects the previously allocated memory
    //!pointed by p can hold.
    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard, gnu::pure ]] static size_type size( const_pointer const p ) noexcept
    {
        return static_cast<size_type>( detail::crt_aligned_alloc_size<alignment>( p ) / sizeof( T ) );
    }

    /// Release unused memory back to the OS.
    static void trim() noexcept
    {
#   if defined( __linux__ )
        ::malloc_trim( 0 );
#   endif
        // Windows: HeapCompact is per-heap and not useful for CRT.
        // macOS:   no public CRT trim API.
    }

    // --- allocator traits ---
    static constexpr bool try_expand_supports_null  { false }; // _expand(nullptr) is UB
    // _expand works in-place for shrink, but only on non-aligned (regular malloc)
    // allocations. Callers with alignment > guaranteed_alignment must use shrink_to.
#ifdef _MSC_VER
    static constexpr bool guaranteed_in_place_shrink{ true };
    // True: _expand only works on regular malloc allocations (not _aligned_malloc).
    // Callers must not call try_expand / try_shrink_in_place when alignment > guaranteed.
    static constexpr bool in_place_ops_require_default_alignment{ true };
#else
    static constexpr bool guaranteed_in_place_shrink            { false };
    static constexpr bool in_place_ops_require_default_alignment{ false };
#endif

    // CRT-specific: MSVC ::_expand for in-place expansion AND shrink.
    // IMPORTANT: Only valid for allocations made via regular malloc (not _aligned_malloc).
    // Callers must check alignment <= guaranteed_alignment before calling.
    // https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/expand
#ifdef _MSC_VER
    [[ nodiscard ]]
    static bool try_expand( pointer const ptr, size_type const target_size ) noexcept
    {
        auto const byte_size{ target_size * sizeof( T ) };
        return ::_expand( ptr, byte_size ) != nullptr;
    }

    [[ nodiscard ]]
    static bool try_shrink_in_place( pointer const ptr, size_type const /*current_size*/, size_type const target_size ) noexcept
    {
        auto const byte_size{ target_size * sizeof( T ) };
        return ::_expand( ptr, byte_size ) != nullptr;
    }
#endif

private:
    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ gnu::cold, gnu::assume_aligned( alignment ) ]]
    [[ nodiscard ]] static pointer do_resize( pointer const existing_allocation_address, size_type const existing_allocation_size, size_type const new_size )
    {
        auto const result{ std::assume_aligned<alignment>( static_cast<pointer>(
            detail::crt_realloc<alignment>( existing_allocation_address, existing_allocation_size * sizeof( T ), new_size * sizeof( T ) )
        ))};
        if ( !result && new_size ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return result;
    }
}; // struct crt_allocator

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
