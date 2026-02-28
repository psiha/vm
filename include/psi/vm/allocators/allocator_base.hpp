////////////////////////////////////////////////////////////////////////////////
///
/// \file allocator_base.hpp
/// ------------------------
///
/// Base providing the N2045 "Version 2" allocator interface atop concrete
/// allocator primitives.
///
/// Derived allocators supply: allocate, deallocate, grow_to, shrink_to, size
/// and optionally try_expand. Methods can be static (stateless allocators)
/// or non-static (stateful allocators). This base provides:
///   - Shared typedefs (value_type, pointer, size_type, ...)
///   - version_type meta-function (boost::container V2 tag)
///   - allocation_command (both Boost.Container and pure-N2045 signatures)
///   - allocate_one / deallocate_one (single-node allocation)
///   - max_size
///   - resize (generic grow_to / shrink_to dispatcher)
///   - allocate_at_least (C++23)
///
/// Dispatch uses C++23 deducing this -- methods are called on an allocator
/// instance, and the correct derived method is found via name lookup
/// regardless of whether it is static or non-static.
///
/// Ref: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2006/n2045.html
///
/// Copyright (c) Domagoj Saric 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once

#include <boost/assert.hpp>
#include <boost/container/detail/allocation_type.hpp>
#include <boost/container/detail/version_type.hpp>

#include <cstdint>
#include <limits>
#include <memory>  // std::allocation_result
#include <type_traits>
#include <utility> // std::pair
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace detail
{
    // https://www.gnu.org/software/libc/manual/html_node/Aligned-Memory-Blocks.html
    inline std::uint8_t constexpr guaranteed_alignment{ 16 }; // all known x64 and arm64 platforms

    // Alignment of T that is safe for incomplete types. Falls back to max_align_t
    // when T is incomplete (forward-declared). This allows allocator class templates
    // to be instantiated with forward-declared types (e.g. heap_storage<FwdDecl>).
    template <typename T>
    inline constexpr std::uint8_t safe_alignof_v{ alignof( std::conditional_t<( requires { sizeof( T ); } ), T, std::max_align_t> ) };

#if PSI_MALLOC_OVERCOMMIT != PSI_OVERCOMMIT_Full
    [[ noreturn, gnu::cold ]] void throw_bad_alloc();
#else
    [[ noreturn, gnu::cold ]] inline void throw_bad_alloc() noexcept
    {
        BOOST_ASSERT_MSG( false, "Unexpected allocation failure" );
        std::unreachable();
    }
#endif
} // namespace detail

// Concept: does the allocator provide try_expand(ptr, size) -> bool?
// Uses instance syntax which works for both static and non-static methods.
template <typename A>
concept has_try_expand = requires( A a, typename A::pointer p, typename A::size_type sz ) {
    { a.try_expand( p, sz ) } -> std::same_as<bool>;
};

// Concept: does the allocator provide try_shrink_in_place(ptr, cur_sz, tgt_sz) -> bool?
template <typename A>
concept has_try_shrink_in_place = requires( A a, typename A::pointer p, typename A::size_type sz ) {
    { a.try_shrink_in_place( p, sz, sz ) } -> std::same_as<bool>;
};

////////////////////////////////////////////////////////////////////////////////
/// \class allocator_base
///
/// Base for N2045 Version 2 allocators. Derived must provide at minimum:
///   pointer   allocate  ( size_type count, void const * hint = nullptr );
///   void      deallocate( pointer ptr, size_type size = 0 ) noexcept;
///   pointer   grow_to   ( pointer cur, size_type cur_sz, size_type tgt_sz );
///   pointer   shrink_to ( pointer cur, size_type cur_sz, size_type tgt_sz ) noexcept;
///   size_type size      ( const_pointer ptr ) noexcept;
///
/// Optionally:
///   bool try_expand( pointer ptr, size_type target_size ) noexcept;
///
////////////////////////////////////////////////////////////////////////////////

template <typename T, typename sz_t = std::size_t>
struct allocator_base
{
    // --- shared typedefs ---
    using value_type      = T;
    using       pointer   = T *;
    using const_pointer   = T const *;
    using       reference = T &;
    using const_reference = T const &;
    using       size_type = sz_t;
    using difference_type = std::make_signed_t<size_type>;

    using allocation_commands = std::uint8_t;

    // Boost.Container V2 version tag. Concrete allocators should define:
    //   using version = version_type<my_allocator>;
    template <typename Derived>
    using version_type = boost::container::dtl::version_type<Derived, 2>;

    // --- max_size ---
    [[ gnu::const ]] static constexpr size_type max_size() noexcept
    {
        return std::numeric_limits<size_type>::max() / sizeof( T );
    }

    // --- resize (generic direction dispatcher) ---
    [[ nodiscard ]] pointer resize( this auto & self, pointer const current_address, size_type const current_size, size_type const target_size )
    {
        if ( target_size >= current_size )
            return self.grow_to  ( current_address, current_size, target_size );
        else
            return self.shrink_to( current_address, current_size, target_size );
    }

    // --- allocate_one / deallocate_one (N2045 single-node allocation) ---
    [[ nodiscard ]] pointer allocate_one( this auto & self ) { return self.allocate( 1 ); }
    void deallocate_one( this auto & self, pointer const p ) noexcept { self.deallocate( p, 1 ); }

    // --- allocate_at_least (C++23 P0401) ---
#if __cpp_lib_allocate_at_least >= 202302L
    using allocation_result = std::allocation_result<pointer>;
#else
    struct allocation_result { pointer ptr; size_type count; };
#endif

    [[ nodiscard ]] allocation_result allocate_at_least( this auto & self, size_type const count )
    {
        auto const ptr{ self.allocate( count ) };
        return { ptr, self.size( ptr ) };
    }

    // -------------------------------------------------------------------
    // allocation_command -- Boost.Container variant (primary)
    //
    // This is the interface used by heap_storage::storage_try_expand_capacity
    // and by boost::container internals.
    //
    //   command:               bitwise OR of boost::container allocation flags
    //   limit_size:            minimum/maximum acceptable size (expand/shrink)
    //   prefer_in_recvd_out:   input = preferred size, output = received size
    //   reuse:                 input = existing block, output = result pointer
    //
    // Returns the (possibly new) pointer on success, nullptr on failure when
    // nothrow_allocation is set.
    // -------------------------------------------------------------------

    [[ nodiscard ]]
    pointer allocation_command
    (
        this auto &               self,
        allocation_commands const command,
        [[ maybe_unused ]]
        size_type           const limit_size,
        size_type &               prefer_in_recvd_out_size,
        pointer   &               reuse
    )
    {
        namespace bc = boost::container;
        using Self = std::remove_reference_t<decltype( self )>;

        BOOST_ASSERT_MSG( !( command & bc::zero_memory ), "zero_memory not implemented" );
        BOOST_ASSERT_MSG
        (
            !!( command & bc::shrink_in_place ) != !!( command & ( bc::allocate_new | bc::expand_fwd | bc::expand_bwd ) ),
            "shrink_in_place conflicts with expand/allocate"
        );

        auto const preferred_size{ prefer_in_recvd_out_size };
        auto const current_size  { reuse ? self.size( reuse ) : size_type{ 0 } };
        bool success{ false };

        // --- expand_fwd: try in-place expansion (if allocator supports it) ---
        if ( reuse && ( command & bc::expand_fwd ) )
        {
            if constexpr ( has_try_expand<Self> )
            {
                if ( self.try_expand( reuse, preferred_size ) )
                    success = true;
            }
            // For allocators without try_expand: expand_fwd silently fails,
            // falls through to allocate_new if that flag is also set.
        }

        // --- shrink_in_place ---
        if ( !success && reuse && ( command & ( bc::shrink_in_place | bc::try_shrink_in_place ) ) )
        {
            BOOST_ASSUME( preferred_size <= current_size );
            reuse   = self.shrink_to( reuse, current_size, preferred_size );
            success = true;
        }

        // --- allocate_new ---
        if ( !success && ( command & bc::allocate_new ) )
        {
            reuse   = self.allocate( preferred_size );
            success = true; // allocate() throws on failure
        }

        if ( success ) [[ likely ]]
        {
            BOOST_ASSUME( reuse );
            prefer_in_recvd_out_size = self.size( reuse );
            return reuse;
        }

        if ( !( command & bc::nothrow_allocation ) )
            detail::throw_bad_alloc();

        return nullptr;
    }

    // -------------------------------------------------------------------
    // allocation_command -- pure N2045 paper variant (wrapper)
    //
    //   Returns: { pointer, was_expanded_in_place }
    //   pointer = nullptr on failure if nothrow_allocation is set.
    // -------------------------------------------------------------------

    [[ nodiscard ]]
    std::pair<pointer, bool> allocation_command
    (
        this auto &                 self,
        allocation_commands   const command,
        size_type             const limit_size,
        size_type             const preferred_size,
        size_type           &       received_size,
        pointer                     reuse_ptr = nullptr
    )
    {
        auto prefer_recv{ preferred_size };
        auto reuse      { reuse_ptr      };
        auto const result{ self.allocation_command( command, limit_size, prefer_recv, reuse ) };
        received_size = prefer_recv;
        // expanded in-place: result == original reuse_ptr and it's non-null
        bool const expanded_in_place{ result && result == reuse_ptr };
        return { result, expanded_in_place };
    }
}; // struct allocator_base

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
