////////////////////////////////////////////////////////////////////////////////
///
/// \file tcmalloc.hpp
/// -------------------
///
/// Allocator wrapper for Google's tcmalloc (https://github.com/google/tcmalloc).
/// Derives from allocator_base for the N2045 Version 2 interface.
///
/// Features:
///   - tc_memalign / tc_realloc: aligned allocation + reallocation
///   - tc_malloc_size: query actual allocation size
///   - No try_expand: tcmalloc does not expose an in-place expansion API
///
/// Requires: PSI_VM_TCMALLOC cmake option enabled.
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
//------------------------------------------------------------------------------
// tcmalloc provides these as C functions (either via override or explicit prefix)
extern "C"
{
    void *      tc_memalign   ( std::size_t alignment, std::size_t size ) noexcept;
    void *      tc_malloc     ( std::size_t size ) noexcept;
    void *      tc_realloc    ( void * ptr, std::size_t size ) noexcept;
    void        tc_free       ( void * ptr ) noexcept;
    std::size_t tc_malloc_size( const void * ptr ) noexcept;
    void        MallocExtension_ReleaseFreeMemory() noexcept;
}
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

template <typename T, typename sz_t = std::size_t>
struct tcmalloc_allocator
    : allocator_base<T, sz_t>
{
    using base = allocator_base<T, sz_t>;
    using version = base::template version_type<tcmalloc_allocator>;
    using value_type    = T;
    using pointer       = T *;
    using const_pointer = T const *;
    using size_type     = sz_t;

    template <class U> struct rebind { using other = tcmalloc_allocator<U, sz_t>; };

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard, using gnu: cold, assume_aligned( alignment ), malloc, returns_nonnull ]]
    static pointer allocate( size_type const count, void const * const /*hint*/ = nullptr )
    {
        BOOST_ASSUME( count < base::max_size() );
        auto const byte_size{ count * sizeof( T ) };
        void * new_allocation;
        if constexpr ( alignment > alignof( std::max_align_t ) )
            new_allocation = ::tc_memalign( alignment, byte_size );
        else
            new_allocation = ::tc_malloc( byte_size );

        if ( !new_allocation ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return static_cast<pointer>( new_allocation );
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    static void deallocate( pointer const ptr, size_type const /*size*/ = 0 ) noexcept
    {
        ::tc_free( ptr );
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]]
    static pointer grow_to( pointer const current_address, size_type const /*current_size*/, size_type const target_size )
    {
        auto const byte_size{ target_size * sizeof( T ) };
        auto * const result{ static_cast<pointer>( ::tc_realloc( current_address, byte_size ) ) };
        if ( !result ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return result;
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]]
    static pointer shrink_to( pointer const current_address, size_type const current_size, size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= current_size );
        auto const byte_size{ target_size * sizeof( T ) };
        auto * const result{ static_cast<pointer>( ::tc_realloc( current_address, byte_size ) ) };
        return result ? result : current_address;
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ gnu::pure ]]
    static size_type size( const_pointer const ptr ) noexcept
    {
        return static_cast<size_type>( ::tc_malloc_size( ptr ) / sizeof( T ) );
    }

    /// Release free memory back to the OS.
    static void trim() noexcept
    {
        ::MallocExtension_ReleaseFreeMemory();
    }

    // --- allocator traits ---
    static constexpr bool try_expand_supports_null              { false };
    static constexpr bool guaranteed_in_place_shrink            { false }; // tc_realloc may relocate
    static constexpr bool in_place_ops_require_default_alignment{ false }; // n/a (no in-place ops)

    // tcmalloc does not expose an in-place expansion or shrink API.
    // No try_expand / try_shrink_in_place -- heap_storage uses allocate+move+free path.
}; // struct tcmalloc_allocator

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
