////////////////////////////////////////////////////////////////////////////////
///
/// \file mi_scoped_heap.hpp
/// -------------------------
///
/// Scoped-heap allocator using mimalloc's per-heap API. All allocations made
/// within an mi_heap_scope RAII guard are placed on a dedicated mi_heap_t.
/// When the scope ends, all allocations can be freed atomically (destroy) or
/// transferred to the default heap (release).
///
/// Thread-safe: the active heap is a thread_local, so scopes on different
/// threads are independent.
///
/// Usage with psi::vm containers:
///
///   using pool_alloc = mi_scoped_heap_allocator<int>;
///   using pool_vec   = vector<heap_storage<int, std::size_t, pool_alloc>>;
///
///   {
///       mi_heap_scope scope;
///       pool_vec v;
///       v.push_back(42);
///       // v's allocation lives on the scoped heap
///   } // scope dtor: mi_heap_destroy frees everything at once
///
///   // Or, to keep allocations alive past the scope:
///   {
///       mi_heap_scope scope;
///       pool_vec v;
///       v.push_back(42);
///       scope.release(); // transfer allocations to default heap
///   } // allocations survive, freed individually via mi_free
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
    // Thread-local active scoped heap. nullptr = no scope active (use default).
    inline thread_local mi_heap_t * p_scoped_heap{ nullptr };
} // namespace detail


////////////////////////////////////////////////////////////////////////////////
/// \class mi_heap_scope
///
/// RAII guard that creates a mimalloc heap and sets it as the active scoped
/// heap for the current thread. Nesting is NOT supported — only one scope
/// may be active per thread at a time.
///
/// Destruction modes:
///   - Default (destructor): mi_heap_destroy -- atomically frees ALL
///     allocations made on this heap. O(1) bulk deallocation.
///   - release(): mi_heap_delete -- transfers remaining allocations to the
///     default heap. They become individually freeable via mi_free.
////////////////////////////////////////////////////////////////////////////////

struct mi_heap_scope
{
    mi_heap_scope()
    {
        BOOST_ASSERT_MSG( !detail::p_scoped_heap, "Nested mi_heap_scope not supported" );
        detail::p_scoped_heap = ::mi_heap_new();
        if ( !detail::p_scoped_heap ) [[ unlikely ]]
            detail::throw_bad_alloc();
    }

    ~mi_heap_scope() noexcept
    {
        if ( detail::p_scoped_heap ) // skip if already released
            ::mi_heap_destroy( detail::p_scoped_heap );
        detail::p_scoped_heap = nullptr;
    }

    mi_heap_scope( mi_heap_scope const & ) = delete;
    mi_heap_scope & operator=( mi_heap_scope const & ) = delete;

    /// Transfer all remaining allocations to the default heap instead of
    /// destroying them. Call this when some containers allocated on the
    /// scoped heap need to outlive the scope.
    void release() noexcept
    {
        BOOST_ASSERT_MSG( detail::p_scoped_heap, "Already released" );
        ::mi_heap_delete( detail::p_scoped_heap ); // transfers allocations, frees heap handle
        detail::p_scoped_heap = nullptr;
    }

    /// Access the underlying mi_heap_t (for direct API calls if needed).
    [[ nodiscard ]] mi_heap_t * heap() const noexcept { return detail::p_scoped_heap; }
}; // struct mi_heap_scope


////////////////////////////////////////////////////////////////////////////////
/// \class mi_scoped_heap_allocator
///
/// Allocator that routes through the thread-local active mi_heap_scope (if
/// one is active) or falls back to default mimalloc behavior. Conforms to
/// the allocator_base CRTP interface (all static methods).
///
/// Use with heap_storage:
///   heap_storage<T, sz_t, mi_scoped_heap_allocator<T, sz_t>>
////////////////////////////////////////////////////////////////////////////////

template <typename T, typename sz_t = std::size_t>
struct mi_scoped_heap_allocator
    : allocator_base<T, sz_t>
{
    using base = allocator_base<T, sz_t>;
    using version = base::template version_type<mi_scoped_heap_allocator>;
    using value_type    = T;
    using pointer       = T *;
    using const_pointer = T const *;
    using size_type     = sz_t;

    template <class U> struct rebind { using other = mi_scoped_heap_allocator<U, sz_t>; };

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
    static pointer grow_to( pointer const current_address, size_type const /*current_size*/, size_type const target_size )
    {
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

    [[ gnu::pure ]]
    static size_type size( const_pointer const ptr ) noexcept
    {
        return static_cast<size_type>( ::mi_usable_size( ptr ) / sizeof( T ) );
    }

    /// Release unused memory back to the OS. Collects the scoped heap if
    /// one is active, otherwise performs a global mimalloc collection.
    static void trim() noexcept
    {
        auto * const heap{ detail::p_scoped_heap };
        if ( heap )
            ::mi_heap_collect( heap, true );
        else
            ::mi_collect( true );
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
}; // struct mi_scoped_heap_allocator

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
