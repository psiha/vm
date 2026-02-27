////////////////////////////////////////////////////////////////////////////////
///
/// \file jemalloc.hpp
/// -------------------
///
/// Allocator wrapper for jemalloc (https://github.com/jemalloc/jemalloc).
/// Derives from allocator_base for the N2045 Version 2 interface.
///
/// Features:
///   - aligned_alloc / je_rallocx: aligned allocation + reallocation
///   - je_xallocx: in-place expansion (try_expand support)
///   - je_sallocx: query actual allocation size
///
/// Requires: PSI_VM_JEMALLOC cmake option enabled.
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

#include <jemalloc/jemalloc.h>

#include <cstddef>
#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

template <typename T, typename sz_t = std::size_t>
struct jemalloc_allocator
    : allocator_base<T, sz_t>
{
    using base = allocator_base<T, sz_t>;
    using version = base::template version_type<jemalloc_allocator>;
    using value_type    = T;
    using pointer       = T *;
    using const_pointer = T const *;
    using size_type     = sz_t;

    template <class U> struct rebind { using other = jemalloc_allocator<U, sz_t>; };

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    static int consteval je_flags() noexcept { return ( alignment > alignof( std::max_align_t ) ) ? MALLOCX_ALIGN( alignment ) : 0; }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard, using gnu: cold, assume_aligned( alignment ), malloc, returns_nonnull ]]
    static pointer allocate( size_type const count, void const * const /*hint*/ = nullptr )
    {
        BOOST_ASSUME( count < base::max_size() );
        auto * const ptr{ static_cast<pointer>( ::je_mallocx( count * sizeof( T ), je_flags<alignment>() ) ) };
        if ( !ptr ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return ptr;
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    static void deallocate( pointer const ptr, size_type const size = 0 ) noexcept
    {
        if ( size )
            ::je_sdallocx( ptr, size * sizeof( T ), je_flags<alignment>() );
        else
            ::je_dallocx( ptr, je_flags<alignment>() );
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]]
    static pointer grow_to( pointer const current_address, size_type const /*current_size*/, size_type const target_size )
    {
        auto * const ptr{ static_cast<pointer>( ::je_rallocx( current_address, target_size * sizeof( T ), je_flags<alignment>() ) ) };
        if ( !ptr ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return ptr;
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]]
    static pointer shrink_to( pointer const current_address, size_type const current_size, size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= current_size );
        auto * const ptr{ static_cast<pointer>( ::je_rallocx( current_address, target_size * sizeof( T ), je_flags<alignment>() ) ) };
        return ptr ? ptr : current_address;
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ gnu::pure ]]
    static size_type size( const_pointer const ptr ) noexcept
    {
        return static_cast<size_type>( ::je_sallocx( ptr, je_flags<alignment>() ) / sizeof( T ) );
    }

    // --- allocator traits ---
    static constexpr bool try_expand_supports_null              { false };
    static constexpr bool guaranteed_in_place_shrink            { false }; // je_rallocx may relocate
    static constexpr bool in_place_ops_require_default_alignment{ false }; // jemalloc handles any alignment

    /// Try to expand in-place via xallocx. Returns true if the allocation
    /// now has at least target_size elements.
    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]]
    static bool try_expand( pointer const ptr, size_type const target_size ) noexcept
    {
        auto const byte_size{ target_size * sizeof( T ) };
        auto const actual{ ::je_xallocx( ptr, byte_size, 0, je_flags<alignment>() ) };
        return actual >= byte_size;
    }

    /// Try to shrink in-place via xallocx. Returns true if it stayed in-place.
    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]]
    static bool try_shrink_in_place( pointer const ptr, size_type const /*current_size*/, size_type const target_size ) noexcept
    {
        auto const byte_size{ target_size * sizeof( T ) };
        auto const actual{ ::je_xallocx( ptr, byte_size, 0, je_flags<alignment>() ) };
        return actual <= byte_size; // xallocx may return >= requested; <= means it shrank
    }
}; // struct jemalloc_allocator

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
