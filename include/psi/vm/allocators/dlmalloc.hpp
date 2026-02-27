////////////////////////////////////////////////////////////////////////////////
///
/// \file dlmalloc.hpp
/// -------------------
///
/// Allocator wrapper for dlmalloc via Boost.Container's alloc_lib interface.
/// Derives from allocator_base for the N2045 Version 2 interface.
///
/// Features:
///   - boost_cont_malloc / boost_cont_free: allocation/deallocation
///   - boost_cont_memalign: aligned allocation
///   - boost_cont_grow: in-place expansion (try_expand support)
///   - boost_cont_shrink: in-place shrink
///   - boost_cont_size: query actual allocation size
///
/// Requires the compiled Boost.Container allocator library (alloc_lib.c)
/// which provides the boost_cont_* C functions. Define PSI_VM_HAS_DLMALLOC=1
/// when this library is available (rama builds define it automatically).
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

#include <cstddef>
#include <cstdint>
#include <cstring> // memcpy
//------------------------------------------------------------------------------
// Forward-declare the boost.container dlmalloc wrapper C functions.
// This avoids including alloc_lib.h which has typedef conflicts when
// included from within the psi::vm namespace.
extern "C"
{
    void *      boost_cont_malloc  ( std::size_t bytes ) noexcept;
    void        boost_cont_free    ( void * mem ) noexcept;
    void *      boost_cont_memalign( std::size_t bytes, std::size_t alignment ) noexcept;
    std::size_t boost_cont_size    ( void const * p ) noexcept;
    int         boost_cont_grow    ( void * oldmem, std::size_t minbytes, std::size_t maxbytes, std::size_t * received ) noexcept;
    int         boost_cont_shrink  ( void * oldmem, std::size_t minbytes, std::size_t maxbytes, std::size_t * received, int do_commit ) noexcept;
} // extern "C"
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

template <typename T, typename sz_t = std::size_t>
struct dlmalloc_allocator
    : allocator_base<T, sz_t>
{
    using base = allocator_base<T, sz_t>;
    using version = base::template version_type<dlmalloc_allocator>;
    using value_type    = T;
    using pointer       = T *;
    using const_pointer = T const *;
    using size_type     = sz_t;

    template <class U> struct rebind { using other = dlmalloc_allocator<U, sz_t>; };

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]] [[ using gnu: cold, assume_aligned( alignment ), malloc, returns_nonnull ]]
    static pointer allocate( size_type const count, void const * const /*hint*/ = nullptr )
    {
        BOOST_ASSUME( count < base::max_size() );
        auto const byte_size{ count * sizeof( T ) };
        void * new_allocation;
        if constexpr ( alignment > detail::guaranteed_alignment )
            new_allocation = ::boost_cont_memalign( byte_size, alignment );
        else
            new_allocation = ::boost_cont_malloc( byte_size );

        if ( !new_allocation ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return static_cast<pointer>( new_allocation );
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    static void deallocate( pointer const ptr, size_type const /*size*/ = 0 ) noexcept
    {
        ::boost_cont_free( ptr );
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]]
    static pointer grow_to( pointer const current_address, size_type const current_size, size_type const target_size )
    {
        BOOST_ASSUME( target_size >= current_size );
        auto const target_bytes{ target_size * sizeof( T ) };

        // Try in-place growth first (very cheap -- dlmalloc is good at this)
        std::size_t received{ 0 };
        if ( ::boost_cont_grow( current_address, target_bytes, target_bytes, &received ) )
            return current_address;

        // In-place failed: allocate new, copy, free old
        auto * const new_alloc{ allocate<alignment>( target_size ) };
        std::memcpy( new_alloc, current_address, current_size * sizeof( T ) );
        ::boost_cont_free( current_address );
        return new_alloc;
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]]
    static pointer shrink_to( pointer const current_address, size_type const current_size, size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= current_size );
        if ( !target_size )
        {
            ::boost_cont_free( current_address );
            return nullptr;
        }
        auto const target_bytes{ target_size * sizeof( T ) };
        std::size_t received{ 0 };
        ::boost_cont_shrink( current_address, target_bytes, target_bytes, &received, true );
        return current_address; // dlmalloc shrink always succeeds (stays in-place)
    }

    /// Query the actual usable size of the allocation.
    [[ gnu::pure ]]
    static size_type size( const_pointer const ptr ) noexcept
    {
        return static_cast<size_type>( ::boost_cont_size( ptr ) / sizeof( T ) );
    }

    // --- allocator traits ---
    static constexpr bool try_expand_supports_null              { false };
    static constexpr bool guaranteed_in_place_shrink            { true  }; // boost_cont_shrink always in-place
    static constexpr bool in_place_ops_require_default_alignment{ false }; // dlmalloc handles any alignment

    /// Try to expand the allocation in-place. Returns true if it succeeded.
    [[ nodiscard ]]
    static bool try_expand( pointer const ptr, size_type const target_size ) noexcept
    {
        auto const target_bytes{ target_size * sizeof( T ) };
        std::size_t received{ 0 };
        return ::boost_cont_grow( ptr, target_bytes, target_bytes, &received ) != 0;
    }

    /// Shrink allocation in-place. Always succeeds for dlmalloc.
    [[ nodiscard ]]
    static bool try_shrink_in_place( pointer const ptr, size_type const /*current_size*/, size_type const target_size ) noexcept
    {
        auto const target_bytes{ target_size * sizeof( T ) };
        std::size_t received{ 0 };
        return ::boost_cont_shrink( ptr, target_bytes, target_bytes, &received, true ) != 0;
    }
}; // struct dlmalloc_allocator

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
