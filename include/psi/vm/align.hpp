//------------------------------------------------------------------------------
#pragma once

#include <psi/build/has_builtin.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>

#include <cstdint>
#include <std_fix/bit>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 5030 ) // unknown attribute
#endif

namespace align_detail
{
    [[ gnu::const ]] constexpr auto generic_divide_up( auto const numerator, auto const denominator ) noexcept
    {
        return static_cast<decltype( numerator )>( ( numerator + denominator - 1 ) / denominator );
    }
} // namespace detail

#ifdef __clang__
[[ using gnu: const, always_inline ]] constexpr auto is_aligned( auto const value, auto const alignment ) noexcept { return __builtin_is_aligned( value, alignment ); }
#else
[[ using gnu: const, always_inline ]] constexpr auto is_aligned( auto   const value, auto const alignment ) noexcept { return value % alignment == 0; }
[[ using gnu: const, always_inline ]] constexpr auto is_aligned( auto * const ptr  , auto const alignment ) noexcept { return is_aligned( std::bit_cast<std::uintptr_t>( ptr ), alignment ); }
#endif
[[ using gnu: const, always_inline ]] constexpr auto align_down( auto const value, auto const alignment ) noexcept
{
    using T = decltype( value );
    auto const is_power_of_2{ std::has_single_bit( unsigned( alignment ) ) };
    BOOST_ASSUME( is_power_of_2 );
#if __has_builtin( __builtin_constant_p )
    if ( __builtin_constant_p( alignment ) )
        return __builtin_align_down( value, alignment );
    else
#endif
    if constexpr ( std::is_pointer_v<T> )
        return std::bit_cast<T>( align_down( std::bit_cast<std::uintptr_t>( value ), alignment ) );
    else
        return static_cast<T>( value & ~( T( alignment ) - 1 ) );
}
[[ using gnu: const, always_inline ]] constexpr auto align_up( auto const value, auto const alignment ) noexcept
{
    using T = decltype( value );
    auto const is_power_of_2{ std::has_single_bit( unsigned( alignment ) ) };
    BOOST_ASSUME( is_power_of_2 );
#if __has_builtin( __builtin_constant_p )
    if ( __builtin_constant_p( alignment ) )
        return __builtin_align_up( value, alignment );
    else
#endif
    if constexpr ( std::is_pointer_v<T> )
        return std::bit_cast<T>( align_up( std::bit_cast<std::uintptr_t>( value ), alignment ) );
    else
        return static_cast<T>( ( value + alignment - 1 ) & ~( T( alignment ) - 1 ) );
}

template <unsigned alignment> [[ using gnu: const, always_inline ]] constexpr auto align_down( auto const value ) noexcept { return align_down( value, alignment ); }
template <unsigned alignment> [[ using gnu: const, always_inline ]] constexpr auto align_up  ( auto const value ) noexcept { return align_up  ( value, alignment ); }

[[ using gnu: const, always_inline ]]
constexpr auto divide_up( auto const numerator, auto const denominator ) noexcept
{
#if __has_builtin( __builtin_constant_p )
    if ( __builtin_constant_p( denominator ) && /*is power of 2*/std::has_single_bit( unsigned( denominator ) ) )
        return static_cast<decltype( numerator )>( align_up( numerator, denominator ) / denominator );
    else
#endif // GCC&co.
        return align_detail::generic_divide_up( numerator, denominator );
}

#ifdef _MSC_VER
#pragma warning( pop )
#endif

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
