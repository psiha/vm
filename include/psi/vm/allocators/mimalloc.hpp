////////////////////////////////////////////////////////////////////////////////
///
/// \file mimalloc.hpp
/// -------------------
///
/// Allocator wrapper for Microsoft's mimalloc (https://github.com/microsoft/mimalloc).
/// Derives from allocator_base for the N2045 Version 2 interface.
///
/// Features:
///   - mi_malloc_aligned / mi_realloc_aligned: aligned allocation + growth
///   - mi_expand: in-place expansion (try_expand support)
///   - mi_usable_size: query actual allocation size
///
/// Requires: PSI_VM_MIMALLOC cmake option enabled (pulls mimalloc via FetchContent).
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once

#include <psi/vm/allocators/allocator_base.hpp>

#include <boost/assert.hpp>

#include <mimalloc.h>

#include <cstddef>
#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace detail
{
    // Thread-local active scoped heap. nullptr = use default mimalloc behavior.
    // Shared with mi_scoped_heap_allocator (mi_scoped_heap.hpp).
    inline thread_local mi_heap_t * p_scoped_heap{ nullptr };
} // namespace detail

template <typename T, typename sz_t = std::size_t>
struct mimalloc_allocator
    : allocator_base<T, sz_t>
{
    using base          = allocator_base<T, sz_t>;
    using version       = base::template version_type<mimalloc_allocator>;
    using value_type    = T;
    using pointer       = T *;
    using const_pointer = T const *;
    using size_type     = sz_t;

    template <class U> struct rebind { using other = mimalloc_allocator<U, sz_t>; };

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]] [[ using gnu: cold, assume_aligned( alignment ), malloc, returns_nonnull ]]
    static pointer allocate( size_type const count, void const * const /*hint*/ = nullptr )
    {
        BOOST_ASSUME( count < base::max_size() );
        auto const byte_size{ count * sizeof( T ) };
        void * p;
        auto * const heap{ detail::p_scoped_heap };
        if constexpr ( alignment > alignof( std::max_align_t ) )
        {
            p = heap ? ::mi_heap_malloc_aligned( heap, byte_size, alignment )
                     : ::mi_malloc_aligned( byte_size, alignment );
        }
        else
        {
            p = heap ? ::mi_heap_malloc( heap, byte_size )
                     : ::mi_malloc( byte_size );
        }
        if ( !p ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return static_cast<pointer>( p );
    }

    // mi_free works regardless of which heap the allocation came from.
    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    static void deallocate( pointer const ptr, size_type const /*size*/ = 0 ) noexcept
    {
        ::mi_free( ptr );
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]]
    static pointer grow_to( pointer const current_address, size_type const current_size, size_type const target_size )
    {
        BOOST_ASSUME( target_size >= current_size );
        auto const byte_size{ target_size * sizeof( T ) };
        void * p;
        auto * const heap{ detail::p_scoped_heap };
        if constexpr ( alignment > alignof( std::max_align_t ) )
        {
            p = heap ? ::mi_heap_realloc_aligned( heap, current_address, byte_size, alignment )
                     : ::mi_realloc_aligned( current_address, byte_size, alignment );
        }
        else
        {
            p = heap ? ::mi_heap_realloc( heap, current_address, byte_size )
                     : ::mi_realloc( current_address, byte_size );
        }
        if ( !p ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return static_cast<pointer>( p );
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]]
    static pointer shrink_to( pointer const current_address, size_type const current_size, size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= current_size );
        auto const byte_size{ target_size * sizeof( T ) };
        void * p;
        auto * const heap{ detail::p_scoped_heap };
        if constexpr ( alignment > alignof( std::max_align_t ) )
        {
            p = heap ? ::mi_heap_realloc_aligned( heap, current_address, byte_size, alignment )
                     : ::mi_realloc_aligned( current_address, byte_size, alignment );
        }
        else
        {
            p = heap ? ::mi_heap_realloc( heap, current_address, byte_size )
                     : ::mi_realloc( current_address, byte_size );
        }
        return static_cast<pointer>( p ? p : current_address );
    }

    /// Query the actual usable size of the allocation pointed to by ptr.
    /// mi_usable_size works regardless of alignment — no alignment param needed.
    [[ gnu::pure ]]
    static size_type size( const_pointer const ptr ) noexcept
    {
        return static_cast<size_type>( ::mi_usable_size( ptr ) / sizeof( T ) );
    }

    // --- allocator traits ---
    static constexpr bool try_expand_supports_null              { false };
    static constexpr bool guaranteed_in_place_shrink            { false }; // mi_realloc may relocate
    static constexpr bool in_place_ops_require_default_alignment{ false }; // mi_expand works for any alignment

    /// Try to expand the allocation in-place (without moving). Returns true
    /// if the allocation at ptr now has at least target_size elements.
    [[ nodiscard ]]
    static bool try_expand( pointer const ptr, size_type const target_size ) noexcept
    {
        auto const byte_size{ target_size * sizeof( T ) };
        return ::mi_expand( ptr, byte_size ) != nullptr;
    }
}; // struct mimalloc_allocator

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
