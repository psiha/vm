////////////////////////////////////////////////////////////////////////////////
/// Yet another take on prior art a la boost::container::static_vector and
/// std::inplace_vector with emphasis on improved debuggability w/o custom type
/// visualizers (i.e. seeing the contained values rather than random bytes),
/// maximum efficiency (avoiding conditionals and dynamic memcpy calls for small
/// vectors - utilizing large SIMD registers for inlined copies with a handful
/// of instructions) and configurability (e.g. overflow handler), in addition
/// to extentions provided by vector_impl.
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

struct assert_on_overflow { // VS17.12.3 MSVC still does not support static operator()
    [[ noreturn ]] void operator()() const noexcept {
        BOOST_ASSERT_MSG( false, "Static vector overflow!" );
        std::unreachable();
    }
}; // assert_on_overflow
struct throw_on_overflow {
    [[ noreturn ]] void operator()() const { detail::throw_out_of_range(); }
}; // throw_on_overflow

template <typename T, std::uint32_t maximum_size, auto overflow_handler = assert_on_overflow{}>
class [[ clang::trivial_abi ]] static_vector
    :
    public vector_impl<static_vector<T, maximum_size, overflow_handler>, T, typename boost::uint_value_t<maximum_size>::least>
{
public:
    using size_type  = typename boost::uint_value_t<maximum_size>::least;
    using value_type = T;

    static size_type constexpr static_capacity{ maximum_size };

private:
    using base = vector_impl<static_vector<T, maximum_size, overflow_handler>, T, typename boost::uint_value_t<maximum_size>::least>;

    // https://github.com/llvm/llvm-project/issues/54535
    // https://github.com/llvm/llvm-project/issues/42585
    // https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/X86/X86Subtarget.h#L78
    // https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/ARM/ARMSubtarget.h#L216
    static auto constexpr unconditional_fixed_memcopy_size_limit
    {
#   ifdef __AVX512F__
        256
#   else
        128
#   endif
    };
    struct this_pod { size_type _0; noninitialized_array<T, maximum_size> _1; }; // verified in storage_grow_to()
    static bool constexpr fixed_sized_copy{ std::is_trivially_copy_constructible_v<T> && ( sizeof( this_pod ) <= unconditional_fixed_memcopy_size_limit ) };
    static bool constexpr fixed_sized_move{      is_trivially_moveable            <T> && ( sizeof( this_pod ) <= unconditional_fixed_memcopy_size_limit ) };

public:
    using base::base;
    constexpr static_vector() noexcept : size_{ 0 } {}
    constexpr explicit static_vector( static_vector const & other ) noexcept( std::is_nothrow_copy_constructible_v<T> )
    {
        if constexpr ( fixed_sized_copy ) {
            fixed_copy( other );
        } else {
            std::uninitialized_copy_n( other.data(), other.size(), this->data() );
            this->size_ = other.size();
        }
    }
    constexpr static_vector( static_vector && other ) noexcept( std::is_nothrow_move_constructible_v<T> )
    {
        if constexpr ( fixed_sized_move ) {
            fixed_copy( other );
        } else {
            std::uninitialized_move_n( other.data(), other.size(), this->data() );
            this->size_ = other.size();
        }
        other.size_ = 0;
    }
    constexpr static_vector & operator=( static_vector const & other ) noexcept( std::is_nothrow_copy_constructible_v<T> )
    {
        if constexpr ( fixed_sized_copy ) {
            std::destroy_n( data(), size() );
            fixed_copy( other );
            return *this;
        } else {
            return static_cast<static_vector &>( base::operator=( other ) );
        }
    }
    constexpr static_vector & operator=( static_vector && other ) noexcept( std::is_nothrow_move_assignable_v<T> )
    {
        if constexpr ( fixed_sized_move ) {
            std::destroy_n( data(), size() );
            return *(new (this) static_vector( std::move( other ) ));
        } else {
            return static_cast<static_vector &>( base::operator=( std::move( other ) ) );
        }
    }

    [[ nodiscard, gnu::pure  ]]        constexpr size_type size    () const noexcept { BOOST_ASSUME( size_ <= maximum_size ); return size_; }
    [[ nodiscard, gnu::const ]] static constexpr size_type capacity()       noexcept { return maximum_size; }

    [[ nodiscard, gnu::const  ]] constexpr value_type       * data()       noexcept { return array_.data; }
    [[ nodiscard, gnu::const  ]] constexpr value_type const * data() const noexcept { return array_.data; }

    void reserve( size_type const new_capacity ) const noexcept { BOOST_ASSUME( new_capacity <= maximum_size ); }

private: friend base; // contiguous storage implementation
    constexpr value_type * storage_init   ( size_type const initial_size ) noexcept( noexcept( overflow_handler() ) ) { return storage_grow_to( initial_size ); }
    constexpr value_type * storage_grow_to( size_type const  target_size ) noexcept( noexcept( overflow_handler() ) )
    {
        static_assert( sizeof( *this ) == sizeof( this_pod ) );
        if ( target_size > maximum_size ) [[ unlikely ]] {
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

private:
    void fixed_copy( static_vector const & __restrict source ) noexcept
    requires( fixed_sized_copy )
    {
        std::memcpy( this, &source, sizeof( *this ) );
        BOOST_ASSUME( this->size_ == source.size() );
    }

private:
    size_type                             size_;
    noninitialized_array<T, maximum_size> array_;
}; // struct static_vector

template <typename T, std::uint32_t maximum_size, auto overflow_handler>
bool constexpr is_trivially_moveable<static_vector<T, maximum_size, overflow_handler>>{ is_trivially_moveable<T> };

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
