////////////////////////////////////////////////////////////////////////////////
/// Fixed capacity vector
///
/// Yet another take on prior art a la boost::container::static_vector and
/// std::inplace_vector with emphasis on:
///  - maximum efficiency:
///    - avoiding dynamic memcpy calls for small vectors - utilizing large
///      registers and/or complex instructions simultaneously operating on
///      multiple registers for inlined copies with a handful of instructions
///    - avoiding conditionals by offering as much information to the optimizer
///      and by being friendly to user assume statements (e.g.
///      assume( !vec.empty() ); prior to a loop to skip the initial loop
///      condition check).
///  - improved debuggability w/o custom type visualizers (i.e. seeing the
///    contained values rather than random bytes)
///  - configurability (e.g. overflow handler)
///  - in addition to extensions provided by vector_impl.
////////////////////////////////////////////////////////////////////////////////
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

#include <psi/vm/containers/vector_impl.hpp>

#include <boost/assert.hpp>
#include <boost/integer.hpp>

#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

template <typename T, std::uint32_t size>
union [[ clang::trivial_abi ]] noninitialized_array // utility for easier debugging: no need for special 'visualizers' over type-erased byte arrays
{
    constexpr  noninitialized_array() noexcept {}
    constexpr ~noninitialized_array() noexcept {}

    T data[ size ];
}; // noninitialized_array

struct assert_on_overflow {
    [[ noreturn ]] static void operator()() noexcept {
        BOOST_ASSERT_MSG( false, "Static vector overflow!" );
        std::unreachable();
    }
}; // assert_on_overflow
struct throw_on_overflow {
    [[ noreturn ]] static void operator()() { detail::throw_out_of_range( "psi::vm::fc_vector overflow" ); }
}; // throw_on_overflow


////////////////////////////////////////////////////////////////////////////////
/// Fixed capacity vector
////////////////////////////////////////////////////////////////////////////////

template <typename T, std::uint32_t capacity_param, auto overflow_handler = assert_on_overflow{}>
class [[ clang::trivial_abi ]] fc_vector
    :
    public vector_impl<fc_vector<T, capacity_param, overflow_handler>, T, typename boost::uint_value_t<capacity_param>::least>
{
public:
    using  size_type = typename boost::uint_value_t<capacity_param>::least;
    using value_type = T;

    static size_type constexpr static_capacity{ capacity_param };

    static bool constexpr storage_zero_initialized{ false };

private:
    using base = vector_impl<fc_vector<T, static_capacity, overflow_handler>, T, size_type>;

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
    static bool constexpr fixed_sized_copy{ std::is_trivially_copy_constructible_v<T> && ( sizeof( this_pod ) <= unconditional_fixed_memcopy_size_limit ) };
    static bool constexpr fixed_sized_move{      is_trivially_moveable            <T> && ( sizeof( this_pod ) <= unconditional_fixed_memcopy_size_limit ) };

public:
    using base::base;
    constexpr fc_vector() noexcept : size_{ 0 } {}
    constexpr explicit fc_vector( fc_vector const & other ) noexcept( std::is_nothrow_copy_constructible_v<T> )
    {
        if constexpr ( fixed_sized_copy ) {
            fixed_copy( other );
        } else {
            std::uninitialized_copy_n( other.data(), other.size(), this->data() );
            this->size_ = other.size();
        }
    }
    constexpr fc_vector( fc_vector && other ) noexcept( std::is_nothrow_move_constructible_v<T> )
    {
        if constexpr ( fixed_sized_move ) {
            fixed_copy( other );
        } else {
            std::uninitialized_move_n( other.data(), other.size(), this->data() );
            this->size_ = other.size();
        }
        other.size_ = 0;
    }
    constexpr fc_vector & operator=( fc_vector const & other ) noexcept( std::is_nothrow_copy_constructible_v<T> )
    {
        if constexpr ( fixed_sized_copy ) {
            destroy_contents();
            fixed_copy( other );
            return *this;
        } else {
            return static_cast<fc_vector &>( base::operator=( other ) );
        }
    }
    constexpr fc_vector & operator=( fc_vector && other ) noexcept( std::is_nothrow_move_assignable_v<T> )
    {
        if constexpr ( fixed_sized_move ) {
            destroy_contents();
            return *(new (this) fc_vector( std::move( other ) ));
        } else {
            return static_cast<fc_vector &>( base::operator=( std::move( other ) ) );
        }
    }

    constexpr ~fc_vector() noexcept { destroy_contents(); }

    [[ nodiscard, gnu::pure  ]]        constexpr size_type size    () const noexcept { BOOST_ASSUME( size_ <= static_capacity ); return size_; }
    [[ nodiscard, gnu::const ]] static constexpr size_type capacity()       noexcept { return static_capacity; }

    [[ nodiscard, gnu::const  ]] constexpr value_type       * data()       noexcept { return array_.data; }
    [[ nodiscard, gnu::const  ]] constexpr value_type const * data() const noexcept { return array_.data; }

    void reserve( size_type const new_capacity ) const noexcept { BOOST_ASSUME( new_capacity <= static_capacity ); }

private: friend base; // contiguous storage implementation
    constexpr value_type * storage_init   ( size_type const initial_size ) noexcept( noexcept( overflow_handler() ) ) { return storage_grow_to( initial_size ); }
    constexpr value_type * storage_grow_to( size_type const  target_size ) noexcept( noexcept( overflow_handler() ) )
    {
        static_assert( sizeof( *this ) == sizeof( this_pod ) );
        if ( target_size > static_capacity ) [[ unlikely ]] {
            overflow_handler();
        }
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
    constexpr void storage_inc_size() noexcept; // TODO

    constexpr void storage_free() noexcept { size_ = 0; }

    constexpr void destroy_contents() noexcept { std::destroy_n( data(), size() ); }

private:
    void fixed_copy( fc_vector const & __restrict source ) noexcept
    requires( fixed_sized_copy )
    {
        // voidptr cast to silence Clang 20 -Wnontrivial-memcall
        std::memcpy( static_cast<void *>( this ), &source, sizeof( *this ) );
        BOOST_ASSUME( this->size_ == source.size() );
    }

private:
    size_type                                size_;
    noninitialized_array<T, static_capacity> array_;
}; // struct fc_vector

template <typename T, std::uint32_t capacity, auto overflow_handler>
bool constexpr is_trivially_moveable<fc_vector<T, capacity, overflow_handler>>{ is_trivially_moveable<T> };

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
