////////////////////////////////////////////////////////////////////////////////
///
/// \file fixed.hpp
/// ----------------
///
/// Storage backend for fixed-capacity (stack-allocated) vectors. No dynamic
/// allocation -- the buffer is embedded directly in the object.
///
/// Optimized copy/move: for small trivially-copyable types, uses unconditional
/// fixed-size memcpy (a handful of register-width instructions) instead of a
/// dynamic memcpy call. The threshold is platform-specific based on available
/// SIMD register width.
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

#include <psi/vm/containers/noninitialized_array.hpp>
#include <psi/vm/containers/is_trivially_moveable.hpp>
#include <psi/vm/containers/abi.hpp> // detail::throw_out_of_range

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>
#include <boost/integer.hpp>

#include <memory>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

struct assert_on_overflow {
    [[ noreturn ]] static void operator()() noexcept {
        BOOST_ASSERT_MSG( false, "Static vector overflow!" );
        std::unreachable();
    }
}; // assert_on_overflow
struct throw_on_overflow {
    [[ noreturn ]] static void operator()() { detail::throw_out_of_range( "psi::vm::fc_vector overflow" ); }
}; // throw_on_overflow


template <typename T, std::uint32_t capacity_param, auto overflow_handler = assert_on_overflow{}>
class [[ clang::trivial_abi ]] fixed_storage
{
public:
    using size_type  = typename boost::uint_value_t<capacity_param>::least;
    using value_type = T;

    static size_type constexpr static_capacity        { capacity_param };
    static bool      constexpr storage_zero_initialized{ false };

    // Platform-specific threshold for unconditional (no-branch) fixed-size memcpy.
    // Below this size the compiler emits inline register moves instead of a
    // dynamic memcpy call -- much faster for small fixed-capacity vectors.
    // https://github.com/llvm/llvm-project/issues/54535
    // https://github.com/llvm/llvm-project/issues/42585
    // https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/X86/X86Subtarget.h#L78
    // https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/ARM/ARMSubtarget.h#L216
    static auto constexpr unconditional_fixed_memcopy_size_limit
    {
#   ifdef __AVX512F__
        256
#   elif defined( __AVX__ ) || defined( __aarch64__ /*LDP&STP*/ )
        128
#   else
         64
#   endif
    };
    struct this_pod { size_type _0; noninitialized_array<T, static_capacity> _1; }; // verified in storage_grow_to()

public:
    // Exposed for vector<fixed_storage> copy optimization: when true, the entire
    // object (size + inline array) can be copied with a single fixed-size memcpy
    // (a handful of register-width instructions) instead of element-wise copy.
    static bool constexpr fixed_sized_copy{ std::is_trivially_copy_constructible_v<T> && ( sizeof( this_pod ) <= unconditional_fixed_memcopy_size_limit ) };

private:
    static bool constexpr fixed_sized_move{ is_trivially_moveable<T> && ( sizeof( this_pod ) <= unconditional_fixed_memcopy_size_limit ) };

public:
    constexpr fixed_storage() noexcept : size_{ 0 } {}

    // Storages manage raw memory -- element-level copy belongs to vector<Storage>.
    fixed_storage( fixed_storage const & ) = delete;
    fixed_storage & operator=( fixed_storage const & ) = delete;

    constexpr fixed_storage( fixed_storage && other ) noexcept( std::is_nothrow_move_constructible_v<T> )
        : size_{ 0 }
    {
        if constexpr ( fixed_sized_move )
        {
            std::memcpy( static_cast<void *>( this ), &other, sizeof( *this ) );
        }
        else
        {
            std::uninitialized_move_n( other.data(), other.size(), data() );
            size_ = other.size_;
        }
        other.size_ = 0;
    }

    constexpr fixed_storage & operator=( fixed_storage && other ) noexcept( std::is_nothrow_move_constructible_v<T> )
    {
        if ( this != &other )
        {
            if constexpr ( !std::is_trivially_destructible_v<T> )
                std::destroy_n( data(), size() );
            if constexpr ( fixed_sized_move )
            {
                std::memcpy( static_cast<void *>( this ), &other, sizeof( *this ) );
            }
            else
            {
                std::uninitialized_move_n( other.data(), other.size(), data() );
                size_ = other.size_;
            }
            other.size_ = 0;
        }
        return *this;
    }

    [[ nodiscard, gnu::pure  ]]        constexpr size_type size    () const noexcept { BOOST_ASSUME( size_ <= static_capacity ); return size_; }
    [[ nodiscard, gnu::const ]] static constexpr size_type capacity()       noexcept { return static_capacity; }
    [[ nodiscard, gnu::pure  ]]        constexpr bool      empty   () const noexcept { return !size_; }

    [[ nodiscard, gnu::const ]] constexpr value_type       * data()       noexcept { return array_.data; }
    [[ nodiscard, gnu::const ]] constexpr value_type const * data() const noexcept { return array_.data; }

    void reserve( size_type const new_capacity ) const noexcept { BOOST_ASSUME( new_capacity <= static_capacity ); }

    // --- storage_* interface for vector_impl ---
    constexpr value_type * storage_init   ( size_type const initial_size ) noexcept( noexcept( overflow_handler() ) ) { return storage_grow_to( initial_size ); }
    constexpr value_type * storage_grow_to( size_type const  target_size ) noexcept( noexcept( overflow_handler() ) )
    {
        static_assert( sizeof( fixed_storage ) == sizeof( this_pod ) );
        if ( target_size > static_capacity ) [[ unlikely ]]
            overflow_handler();
        size_ = target_size;
        return data();
    }
    constexpr value_type * storage_shrink_to( size_type const target_size ) noexcept
    {
        storage_shrink_size_to( target_size );
        return data();
    }
    constexpr void storage_shrink_size_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( size_ >= target_size );
        size_ = target_size;
    }
    constexpr void storage_dec_size() noexcept { BOOST_ASSUME( size_ >= 1 ); --size_; }
    constexpr void storage_inc_size() noexcept { BOOST_ASSUME( size_ < static_capacity ); ++size_; }
    constexpr void storage_free   () noexcept { size_ = 0; }

private:
    size_type                                size_;
    noninitialized_array<T, static_capacity> array_;
}; // class fixed_storage

// fixed_storage is trivially moveable when T is (just memcpy the whole object)
template <typename T, std::uint32_t capacity, auto overflow_handler>
bool constexpr is_trivially_moveable<fixed_storage<T, capacity, overflow_handler>>{ is_trivially_moveable<T> };

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
