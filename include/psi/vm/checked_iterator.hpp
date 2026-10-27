////////////////////////////////////////////////////////////////////////////////
/// The checked iterators of contiguous containers, from the standard library
/// that has them: libc++'s bounded iterator when its hardening ABI option for
/// vector is on, MSVC STL's span iterator when its iterator debugging is on,
/// and a plain pointer otherwise (libstdc++, and builds without checking).
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

#include <std_fix/const_iterator.hpp>

#include <iterator>
//------------------------------------------------------------------------------
namespace psi::vm::detail
{
//------------------------------------------------------------------------------

#if defined( _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR )
    template <typename T> using checked_iterator       = std::__bounded_iter<T *>;
    template <typename T> using checked_const_iterator = std::basic_const_iterator<checked_iterator<T>>;
#elif defined( _ITERATOR_DEBUG_LEVEL ) && _ITERATOR_DEBUG_LEVEL
    template <typename T> using checked_iterator       = std::_Span_iterator<T>;
    template <typename T> using checked_const_iterator = std::basic_const_iterator<checked_iterator<T>>;
#else
    template <typename T> using checked_iterator       = T *;
    template <typename T> using checked_const_iterator = T const *;
#endif

/// The iterator to `ptr` within [begin, end].
template <typename T>
[[ nodiscard ]] constexpr checked_iterator<T> make_checked_iterator( T * const ptr, [[ maybe_unused ]] T * const begin, [[ maybe_unused ]] T * const end ) noexcept
{
#if defined( _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR )
    return std::__make_bounded_iter( ptr, begin, end );
#elif defined( _ITERATOR_DEBUG_LEVEL ) && _ITERATOR_DEBUG_LEVEL
    return std::_Span_iterator{ ptr, begin, end };
#else
    return ptr;
#endif
}

template <typename T>
constexpr T * mutable_iter( T const * const ptr ) noexcept { return const_cast<T *>( ptr ); }

template <typename Base>
constexpr Base mutable_iter( std::basic_const_iterator<Base> const iter ) noexcept { return iter.base(); }
#if defined( _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR )
template <typename T>
auto mutable_iter( std::__bounded_iter<T const *> const iter ) noexcept { return reinterpret_cast<std::__bounded_iter<T *> const &>( iter ); }
#elif defined( _ITERATOR_DEBUG_LEVEL ) && _ITERATOR_DEBUG_LEVEL
template <typename T>
auto mutable_iter( std::_Span_iterator<T const> const iter ) noexcept { return reinterpret_cast<std::_Span_iterator<T> const &>( iter ); }
#endif

//------------------------------------------------------------------------------
} // namespace psi::vm::detail
//------------------------------------------------------------------------------
