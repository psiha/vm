////////////////////////////////////////////////////////////////////////////////
///
/// \file heap.hpp
/// --------------
///
/// Storage backend for heap-allocated vectors. Parameterized by allocator type.
/// When Allocator is void (default), uses mimalloc_allocator (if available) or
/// crt_allocator, with the correct alignment from options.
///
/// The allocator is stored via EBO (private inheritance from allocator_type).
/// Stateless allocators (all-static methods, empty type) add zero overhead.
/// Stateful allocators (holding per-instance state) add their sizeof.
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

#include <psi/vm/allocators/crt.hpp>
#if PSI_VM_HAS_MIMALLOC
#   include <psi/vm/allocators/mimalloc.hpp>
#endif
#include <psi/vm/containers/complete.hpp>
#include <psi/vm/containers/growth_policy.hpp>
#include <psi/vm/containers/is_trivially_moveable.hpp>
#include <psi/vm/containers/trivially_destructible_after_move.hpp>

#include <boost/assert.hpp>

#include <cstddef>
#include <memory>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

struct heap_options
{
    std::uint8_t alignment     { 0    }; // 0 -> default
    bool         cache_capacity{ true }; // if your crt_alloc_size is slow (MSVC)
    // When the value_type is an incomplete/recursive strong-typedef (the vector's
    // support_incomplete_types mode), the class-scope alignment constant must not
    // probe complete<T>/alignof(T): that probe is an ODR hazard that can flip to
    // `true` on a late instantiation and pull the (still-incomplete) value_type's
    // full layout in — a hard recursion for e.g. a std::variant alternative whose
    // own completion is what triggered the query. Fall back to max_align_t instead.
    bool         defer_alignment{ false };
}; // struct heap_options


////////////////////////////////////////////////////////////////////////////////
// \class heap_storage
//
// The Allocator must provide: allocate, deallocate, grow_to, shrink_to, size
// and optionally try_expand (for in-place expansion) and allocation_command.
// Methods can be static (stateless) or non-static (stateful allocators).
//
// The allocator is stored via EBO (private inheritance). For empty/stateless
// allocators this adds zero bytes. For stateful allocators the instance state
// is carried inline.
////////////////////////////////////////////////////////////////////////////////

namespace detail
{
#if PSI_VM_HAS_MIMALLOC
    template <typename sz_t>
    using heap_shell_allocator = mimalloc_allocator<std::byte, sz_t>;
#else
    template <typename sz_t>
    using heap_shell_allocator = crt_allocator<std::byte, sz_t>;
#endif

    // Byte-oriented backing allocator for the default (Allocator=void) heap_storage
    // path.  heap_storage owns a typed T* but allocates through std::byte so
    // the storage class can instantiate while T is still incomplete (recursive
    // strong-typedefs); shell_* below bridge element counts/pointers ↔ bytes.
    // if constexpr — a ternary with alignof(T) is ill-formed for incomplete T even
    // on the discarded branch (both operands are checked at class-template scope).
    template <typename U>
    consteval std::uint8_t heap_default_alignment() noexcept
    {
        if constexpr ( complete<U> )
            return std::uint8_t{ alignof( U ) };
        else
            return std::uint8_t{ alignof( std::max_align_t ) };
    }

    // Resolve the storage alignment with `if constexpr` (NOT a ternary): the
    // heap_default_alignment<U>() branch must not even be *instantiated* when the
    // caller opts out of alignof(U) via options.defer_alignment, because that
    // instantiation evaluates complete<U> — an ODR-hazardous probe that flips to
    // `true` on a late instantiation and drags U's full layout in (a hard
    // recursion for a recursive strong-typedef whose completion is what asked).
    // A ternary operand is always instantiated; `if constexpr` genuinely discards.
    template <typename U, heap_options options>
    consteval std::uint8_t heap_alignment() noexcept
    {
        if constexpr ( options.alignment )
            return options.alignment;
        else if constexpr ( options.defer_alignment )
            return std::uint8_t{ alignof( std::max_align_t ) };
        else
            return heap_default_alignment<U>();
    }
} // namespace detail

template <typename T, typename sz_t = std::size_t, typename Allocator = void, heap_options options = {}>
class [[ nodiscard, clang::trivial_abi ]] heap_storage
    : private std::conditional_t<
        std::is_void_v<Allocator>,
        detail::heap_shell_allocator<sz_t>,
        Allocator
    >
{
    using shell_t = std::conditional_t<std::is_void_v<Allocator>, detail::heap_shell_allocator<sz_t>, Allocator>;

public:
    // Use alignof(T) when T is complete; fall back to max_align_t for incomplete
    // recursive strong-typedefs so heap_storage<> never touches alignof(T) at class scope.
    // With options.defer_alignment (support_incomplete_types), skip the complete<T>
    // probe entirely — even a discarded `if constexpr (complete<T>)` can flip late
    // and recurse into the value_type's still-forming layout (see heap_options).
    static std::uint8_t constexpr alignment{ detail::heap_alignment<T, options>() };

    static bool constexpr storage_zero_initialized{ false };
    static bool constexpr fixed_sized_copy        { false }; // only true for small fixed_storage (MSVC workaround having to define this everywhere)

    using size_type      = sz_t;
    using value_type     = T;
    using pointer        = value_type *;
    using const_pointer  = value_type const *;
    using allocator_type = std::conditional_t<
        std::is_void_v<Allocator>,
        detail::heap_shell_allocator<sz_t>,
        Allocator
    >;

private:
    using al = shell_t; // trait checks (has_try_expand, guaranteed_in_place_shrink, ...)

    [[nodiscard]] static constexpr std::size_t byte_count( size_type const element_count ) noexcept
    {
        return static_cast<std::size_t>( element_count ) * sizeof( value_type );
    }

    [[nodiscard]] static constexpr typename shell_t::size_type shell_byte_count( size_type const element_count ) noexcept
    {
        return static_cast<typename shell_t::size_type>( byte_count( element_count ) );
    }

    constexpr shell_t       & shell()       noexcept { return static_cast<shell_t       &>( *this ); }
    constexpr shell_t const & shell() const noexcept { return static_cast<shell_t const &>( *this ); }

    // Custom Allocator path: delegate to user allocator (element-count semantics).
    constexpr allocator_type       & alloc()       noexcept requires ( !std::is_void_v<Allocator> ) { return static_cast<allocator_type       &>( *this ); }
    constexpr allocator_type const & alloc() const noexcept requires ( !std::is_void_v<Allocator> ) { return static_cast<allocator_type const &>( *this ); }

    // Void-Allocator path: shell_* forward typed element ops to the byte shell (see above).
    [[ nodiscard ]] pointer shell_allocate( size_type const element_count ) { return reinterpret_cast<pointer>( shell().template allocate<alignment>( shell_byte_count( element_count ) ) ); }
    void shell_deallocate( pointer const ptr, size_type const element_capacity ) noexcept { shell().template deallocate<alignment>( reinterpret_cast<typename shell_t::pointer>( ptr ), shell_byte_count( element_capacity ) ); }
    [[ nodiscard ]] pointer shell_grow_to( pointer const ptr, size_type const current_capacity, size_type const target_capacity ) { return reinterpret_cast<pointer>( shell().template grow_to<alignment>( reinterpret_cast<typename shell_t::pointer>( ptr ), shell_byte_count( current_capacity ), shell_byte_count( target_capacity ) ) ); }
    [[ nodiscard ]] pointer shell_shrink_to( pointer const ptr, size_type const current_size, size_type const target_size ) noexcept { return reinterpret_cast<pointer>( shell().template shrink_to<alignment>( reinterpret_cast<typename shell_t::pointer>( ptr ), shell_byte_count( current_size ), shell_byte_count( target_size ) ) ); }
    [[ nodiscard ]] size_type shell_capacity( pointer const ptr ) const noexcept { return static_cast<size_type>( shell().size( reinterpret_cast<typename shell_t::const_pointer>( ptr ) ) / sizeof( value_type ) ); }
    bool shell_try_expand( pointer const ptr, size_type const target_capacity ) noexcept { return shell().try_expand( reinterpret_cast<typename shell_t::pointer>( ptr ), shell_byte_count( target_capacity ) ); }

public:
    constexpr heap_storage() noexcept : shell_t{}, p_array_{ nullptr }, size_{ 0 }, capacity_{ 0 } {}

    // Construct with an explicit allocator (for stateful allocators).
    constexpr explicit heap_storage( allocator_type alloc_init ) noexcept requires ( !std::is_void_v<Allocator> )
        : shell_t{ std::move( alloc_init ) }, p_array_{ nullptr }, size_{ 0 }, capacity_{ 0 } {}

    constexpr ~heap_storage() noexcept
    {
        if ( p_array_ )
            storage_free();
    }

    // Storages manage raw memory -- element-level copy belongs to vector<Storage>.
    heap_storage( heap_storage const & ) = delete;
    heap_storage & operator=( heap_storage const & ) = delete;

    constexpr heap_storage( heap_storage && other ) noexcept
        : shell_t{ static_cast<shell_t &&>( other ) }
        , p_array_ { other.p_array_  }
        , size_    { other.size_     }
        , capacity_{ other.capacity_ }
    { other.mark_freed(); }

    constexpr heap_storage & operator=( heap_storage && other ) noexcept
    {
        if ( p_array_ )
            storage_free();
        static_cast<shell_t &>( *this ) = static_cast<shell_t &&>( other );
        p_array_  = other.p_array_;
        size_     = other.size_;
        if constexpr ( options.cache_capacity )
            capacity_ = other.capacity_;
        other.mark_freed();
        return *this;
    }

    [[ nodiscard, gnu::pure ]] size_type size    () const noexcept { return size_; }
    [[ nodiscard, gnu::pure ]] size_type capacity() const noexcept
    {
        if constexpr ( options.cache_capacity )
        {
            BOOST_ASSUME( capacity_ >= size_ );
            return capacity_;
        }
        else
        {
            if constexpr ( std::is_void_v<Allocator> )
                return empty() ? size_type{} : shell_capacity( p_array_ );
            else
                return empty() ? 0 : alloc().size( p_array_ );
        }
    }

    [[ nodiscard, gnu::pure ]] bool empty() const noexcept { return !size_; }

    [[ nodiscard, gnu::pure, gnu::assume_aligned( alignment ) ]] value_type       * data()       noexcept { return std::assume_aligned<alignment>( p_array_ ); }
    [[ nodiscard, gnu::pure, gnu::assume_aligned( alignment ) ]] value_type const * data() const noexcept { return std::assume_aligned<alignment>( p_array_ ); }

    void reserve( size_type const new_capacity )
    {
        auto const current_cap{ options.cache_capacity ? capacity() : size() };
        if ( new_capacity > current_cap ) {
            do_grow( new_capacity, current_cap );
        }
    }

    constexpr allocator_type get_allocator() const noexcept
    {
        if constexpr ( std::is_void_v<Allocator> )
            return allocator_type{};
        else
            return alloc();
    }

    auto release() noexcept { auto d{ p_array_ }; mark_freed(); return d; }

    void swap( heap_storage & other ) noexcept
    {
        using std::swap;
        swap( static_cast<shell_t &>( *this ), static_cast<shell_t &>( other ) );
        swap( p_array_ , other.p_array_  );
        swap( size_    , other.size_     );
        swap( capacity_, other.capacity_ );
    }

    // Raw element-count allocate/deallocate for external owners (backing stores, etc.).
    // Void-Allocator path uses the byte shell; custom-Allocator path uses element semantics.
    [[nodiscard]] static pointer allocate_external( size_type const element_count )
    {
        if constexpr ( std::is_void_v<Allocator> )
            return reinterpret_cast<pointer>(
                detail::heap_shell_allocator<sz_t>::template allocate<alignment>(
                    static_cast<typename detail::heap_shell_allocator<sz_t>::size_type>( byte_count( element_count ) ) ) );
        else
            return allocator_type::template allocate<alignment>( element_count );
    }

    static void deallocate_external( pointer const ptr, size_type const element_count ) noexcept
    {
        if constexpr ( std::is_void_v<Allocator> )
            detail::heap_shell_allocator<sz_t>::template deallocate<alignment>(
                reinterpret_cast<typename detail::heap_shell_allocator<sz_t>::pointer>( ptr ),
                static_cast<typename detail::heap_shell_allocator<sz_t>::size_type>( byte_count( element_count ) ) );
        else
            allocator_type::template deallocate<alignment>( ptr, element_count );
    }

    // --- storage_* interface for vector<> ---
    [[ using gnu: cold, assume_aligned( alignment ) ]]
    constexpr value_type * storage_init( size_type const initial_size )
    {
        if ( initial_size )
        {
            if constexpr ( std::is_void_v<Allocator> )
                p_array_ = shell_allocate( initial_size );
            else
                p_array_ = alloc().template allocate<alignment>( initial_size );
            size_    = initial_size;
            update_capacity( initial_size );
        }
        else
        {
            mark_freed();
        }
        return data();
    }
    template <geometric_growth G = {1, 1}>
    [[ using gnu: assume_aligned( alignment ), returns_nonnull ]]
    constexpr value_type * storage_grow_to( size_type const target_size )
    {
        auto const current_capacity{ capacity() };
        BOOST_ASSUME( current_capacity >= size_ );
        BOOST_ASSUME( target_size      >= size_ );
        if ( target_size > current_capacity ) [[ unlikely ]] {
            do_grow( G ? G( target_size, current_capacity ) : target_size, current_capacity );
        }
        size_ = target_size;
        return data();
    }

    [[ using gnu: assume_aligned( alignment ), returns_nonnull, cold ]]
    constexpr value_type * storage_shrink_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= size_ );
        // Gate in-place shrink on alignment: CRT's _expand only works on regular malloc allocations.
        constexpr bool effective_guaranteed_in_place_shrink{
            al::guaranteed_in_place_shrink &&
            ( !al::in_place_ops_require_default_alignment || alignment <= detail::guaranteed_alignment )
        };
        if constexpr ( effective_guaranteed_in_place_shrink )
        {
            // Allocator guarantees shrink stays in-place (dlmalloc, MSVC _expand):
            // skip realloc, just update metadata. The allocator may release
            // trailing pages but the pointer is stable.
            if constexpr ( has_try_shrink_in_place<al> )
            {
                if constexpr ( std::is_void_v<Allocator> )
                    BOOST_VERIFY( shell().try_shrink_in_place( reinterpret_cast<typename shell_t::pointer>( p_array_ ), shell_byte_count( size_ ), shell_byte_count( target_size ) ) );
                else
                    BOOST_VERIFY( alloc().try_shrink_in_place( p_array_, size_, target_size ) );
            }
        }
        else
        {
            if constexpr ( std::is_void_v<Allocator> )
                p_array_ = shell_shrink_to( p_array_, size_, target_size );
            else
                p_array_ = alloc().template shrink_to<alignment>( p_array_, size_, target_size );
        }
        BOOST_ASSUME( p_array_ || !target_size );
        BOOST_ASSUME( is_aligned( p_array_, alignment ) );
        storage_shrink_size_to( target_size );
        update_capacity       ( target_size );
        return data();
    }
    constexpr void storage_shrink_size_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( size_ >= target_size );
        size_ = target_size;
    }
    void storage_dec_size() noexcept { BOOST_ASSUME( size_ >= 1 ); --size_; }
    void storage_inc_size() noexcept { BOOST_ASSUME( size_ < capacity() ); ++size_; }

#ifdef _MSC_VER
    bool storage_try_expand_capacity( size_type const target_capacity ) noexcept
    requires( options.cache_capacity && ( !al::in_place_ops_require_default_alignment || alignment <= detail::guaranteed_alignment ) )
    {
        if constexpr ( std::is_void_v<Allocator> )
        {
            if ( shell_try_expand( p_array_, target_capacity ) )
            {
                update_capacity( target_capacity );
                return true;
            }
            return false;
        }
        else
        {
            namespace bc = boost::container;
            auto recv_size{ target_capacity };
            auto reuse    { p_array_ };
            auto const result{ alloc().allocation_command(
                bc::expand_fwd | bc::nothrow_allocation,
                target_capacity, recv_size, reuse
            ) };
            if ( result )
            {
                update_capacity( recv_size );
                return true;
            }
            return false;
        }
    }
#endif

    void storage_free() noexcept
    {
        if constexpr ( std::is_void_v<Allocator> )
            shell_deallocate( data(), options.cache_capacity ? capacity() : size_type{} );
        else
            alloc().template deallocate<alignment>( data(), options.cache_capacity ? capacity() : 0 );
        mark_freed();
    }

private:
    [[ gnu::cold, gnu::noinline, clang::preserve_most ]]
    void do_grow( size_type const target_size, size_type const cached_current_capacity )
    {
        auto const new_capacity{ target_size };

        // Initial allocation (empty vector) -- no existing elements to relocate.
        if ( !p_array_ ) [[ unlikely ]]
        {
            BOOST_ASSUME( !size_ );
            if constexpr ( std::is_void_v<Allocator> )
                p_array_ = shell_allocate( new_capacity );
            else
                p_array_ = alloc().template allocate<alignment>( new_capacity );
            update_capacity( new_capacity );
            return;
        }

        BOOST_ASSUME( cached_current_capacity == capacity() );
        // Gate try_expand on alignment: CRT's _expand only works on regular malloc allocations.
        constexpr bool can_try_expand{
            has_try_expand<al> &&
            ( !al::in_place_ops_require_default_alignment || alignment <= detail::guaranteed_alignment )
        };
        if constexpr ( is_trivially_moveable<T> )
        {
            // Safe to use realloc (bitwise relocation)
            if constexpr ( std::is_void_v<Allocator> )
                p_array_ = shell_grow_to( p_array_, cached_current_capacity, new_capacity );
            else
                p_array_ = alloc().template grow_to<alignment>( p_array_, cached_current_capacity, new_capacity );
        }
        else
        {
            if constexpr ( can_try_expand )
            {
                bool expanded{ false };
                if constexpr ( std::is_void_v<Allocator> )
                    expanded = shell_try_expand( p_array_, new_capacity );
                else
                    expanded = alloc().try_expand( p_array_, new_capacity );
                if ( !expanded )
                    relocate_to( new_capacity, cached_current_capacity );
            }
            else
            {
                relocate_to( new_capacity, cached_current_capacity );
            }
        }
        update_capacity( new_capacity );
    }

    // Relocate all elements to a fresh allocation of new_cap.
    // Only called for non-trivially-moveable T (realloc is unsafe for them).
    // Exception-safe: uninitialized_move_n destroys partially-constructed
    // destination elements on exception; unique_ptr frees the new allocation.
    void relocate_to( size_type const new_cap, size_type const old_cap )
    {
        value_type * new_ptr;
        if constexpr ( std::is_void_v<Allocator> )
            new_ptr = shell_allocate( new_cap );
        else
            new_ptr = alloc().template allocate<alignment>( new_cap );

        auto const free_new{ [ this, new_cap ]( value_type * p ) noexcept {
            if constexpr ( std::is_void_v<Allocator> )
                shell_deallocate( p, new_cap );
            else
                alloc().template deallocate<alignment>( p, new_cap );
        } };
        std::unique_ptr<value_type, decltype( free_new )> guard{ new_ptr, free_new };
        std::uninitialized_move_n( p_array_, size_, guard.get() );
        auto * const relocated{ guard.release() }; // move succeeded, take ownership
        if constexpr ( !trivially_destructible_after_move_assignment<T> )
            std::destroy_n( p_array_, size_ );
        if constexpr ( std::is_void_v<Allocator> )
            shell_deallocate( p_array_, options.cache_capacity ? old_cap : size_type{} );
        else
            alloc().template deallocate<alignment>( p_array_, options.cache_capacity ? old_cap : size_type{} );
        p_array_ = relocated;
    }

    void update_capacity( [[ maybe_unused ]] size_type const requested_capacity ) noexcept
    {
        BOOST_ASSUME( p_array_ || !requested_capacity );
        if constexpr ( options.cache_capacity ) {
#       if defined( _MSC_VER )
            if constexpr ( std::is_void_v<Allocator> )
                BOOST_ASSERT( !requested_capacity || ( shell_capacity( p_array_ ) >= requested_capacity ) );
            else
                BOOST_ASSERT( !requested_capacity || ( alloc().size( p_array_ ) >= requested_capacity ) );
            capacity_ = requested_capacity;
#       else
            if constexpr ( std::is_void_v<Allocator> )
                capacity_ = shell_capacity( p_array_ );
            else
                capacity_ = alloc().size( p_array_ );
            BOOST_ASSUME( capacity_ >= requested_capacity );
#       endif
        }
    }

    // Zero only the data members, preserving the allocator subobject (EBO).
    void mark_freed() noexcept
    {
        p_array_ = nullptr;
        size_    = {};
        if constexpr ( options.cache_capacity )
            capacity_ = {};
    }

    T * __restrict p_array_;
    size_type      size_;
#ifdef _MSC_VER
    [[ msvc::no_unique_address ]]
#else
    [[ no_unique_address ]]
#endif
    std::conditional_t<options.cache_capacity, sz_t, decltype( std::ignore )> capacity_;
}; // class heap_storage

// heap_storage is trivially moveable when the EBO shell allocator is trivially copyable.
template <typename T, typename sz_t, typename Allocator, heap_options options>
bool constexpr is_trivially_moveable<heap_storage<T, sz_t, Allocator, options>>{
    std::is_trivially_copyable_v<
        std::conditional_t<std::is_void_v<Allocator>, detail::heap_shell_allocator<sz_t>, Allocator>
    >
};

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
