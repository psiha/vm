////////////////////////////////////////////////////////////////////////////////
///
/// \file mi_heap.hpp
/// ------------------
///
/// Stateful allocator backed by a specific mi_heap_t instance. Unlike the
/// thread-local mi_scoped_heap_allocator, this allocator carries the heap
/// pointer as instance state and routes ALL allocations through it.
///
/// When used with heap_storage (which stores the allocator via EBO private
/// inheritance), the mi_heap_t* pointer is carried inline in the storage --
/// sizeof(mi_heap_allocator) == sizeof(void*).
///
/// Ownership: the allocator does NOT own the mi_heap_t -- it is a non-owning
/// reference. The caller is responsible for managing the heap lifetime (via
/// mi_heap_scope, or mi_heap_new/mi_heap_destroy directly).
///
/// Usage:
///
///   mi_heap_scope scope;
///   using alloc = mi_heap_allocator<int>;
///   using store = heap_storage<int, std::size_t, alloc>;
///   vector<store> v{ store{ alloc{ scope.heap() } } };
///   v.push_back(42); // allocated on scope's heap
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

template <typename T, typename sz_t = std::size_t>
struct mi_heap_allocator
    : allocator_base<T, sz_t>
{
    using base          = allocator_base<T, sz_t>;
    using version       = base::template version_type<mi_heap_allocator>;
    using value_type    = T;
    using pointer       = T *;
    using const_pointer = T const *;
    using size_type     = sz_t;

    template <class U> struct rebind { using other = mi_heap_allocator<U, sz_t>; };

    // --- construction ---
    constexpr mi_heap_allocator() noexcept : heap_{ nullptr } {} // default: no heap (asserts on use)
    constexpr explicit mi_heap_allocator( mi_heap_t * const heap ) noexcept : heap_{ heap } {}

    // Non-owning reference -- trivially copyable.
    constexpr mi_heap_allocator( mi_heap_allocator const & ) noexcept = default;
    constexpr mi_heap_allocator & operator=( mi_heap_allocator const & ) noexcept = default;

    [[ nodiscard ]] mi_heap_t * heap() const noexcept { return heap_; }

    // --- allocator interface (non-static -- uses instance state) ---

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]] [[ using gnu: cold, assume_aligned( alignment ), malloc, returns_nonnull ]]
    pointer allocate( size_type const count, void const * const /*hint*/ = nullptr )
    {
        BOOST_ASSUME( count < base::max_size() );
        BOOST_ASSERT_MSG( heap_, "mi_heap_allocator: no heap assigned" );
        auto const byte_size{ count * sizeof( T ) };
        void * p;
        if constexpr ( alignment > alignof( std::max_align_t ) )
            p = ::mi_heap_malloc_aligned( heap_, byte_size, alignment );
        else
            p = ::mi_heap_malloc( heap_, byte_size );

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
    pointer grow_to( pointer const current_address, size_type const /*current_size*/, size_type const target_size )
    {
        BOOST_ASSERT_MSG( heap_, "mi_heap_allocator: no heap assigned" );
        auto const byte_size{ target_size * sizeof( T ) };
        void * p;
        if constexpr ( alignment > alignof( std::max_align_t ) )
            p = ::mi_heap_realloc_aligned( heap_, current_address, byte_size, alignment );
        else
            p = ::mi_heap_realloc( heap_, current_address, byte_size );

        if ( !p ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return static_cast<pointer>( p );
    }

    template <std::uint8_t alignment = detail::safe_alignof_v<T>>
    [[ nodiscard ]]
    pointer shrink_to( pointer const current_address, size_type const current_size, size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= current_size );
        BOOST_ASSERT_MSG( heap_, "mi_heap_allocator: no heap assigned" );
        auto const byte_size{ target_size * sizeof( T ) };
        void * p;
        if constexpr ( alignment > alignof( std::max_align_t ) )
            p = ::mi_heap_realloc_aligned( heap_, current_address, byte_size, alignment );
        else
            p = ::mi_heap_realloc( heap_, current_address, byte_size );
        return static_cast<pointer>( p ? p : current_address );
    }

    [[ gnu::pure ]]
    static size_type size( const_pointer const ptr ) noexcept
    {
        return static_cast<size_type>( ::mi_usable_size( ptr ) / sizeof( T ) );
    }

    // --- allocator traits ---
    static constexpr bool try_expand_supports_null              { false };
    static constexpr bool guaranteed_in_place_shrink            { false };
    static constexpr bool in_place_ops_require_default_alignment{ false }; // mi_expand works for any alignment

    // mi_expand works regardless of which heap the allocation came from.
    [[ nodiscard ]]
    static bool try_expand( pointer const ptr, size_type const target_size ) noexcept
    {
        auto const byte_size{ target_size * sizeof( T ) };
        return ::mi_expand( ptr, byte_size ) != nullptr;
    }

private:
    mi_heap_t * heap_;
}; // struct mi_heap_allocator

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
