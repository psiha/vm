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
#include <type_traits>
//------------------------------------------------------------------------------
namespace std
{
#ifdef _LIBCPP_VERSION
inline namespace __1 {
#endif
    template <typename T, typename A> class vector;
    template <class E, class T, class A> class basic_string;
    template <class T, class D> class unique_ptr;
    template <typename S> class function;
#ifdef _LIBCPP_VERSION
} // namespace __1
#endif
} // namespace std
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p1144r12.html std::is_trivially_relocatable
// https://quuxplusone.github.io/blog/2019/02/20/p1144-what-types-are-relocatable
// https://github.com/Quuxplusone/libcxx/blob/trivially-relocatable/test/libcxx/type_traits/is_trivially_relocatable.pass.cpp
// https://brevzin.github.io/c++/2024/10/21/trivial-relocation
// https://github.com/abseil/abseil-cpp/pull/1625
// https://reviews.llvm.org/D114732
// https://github.com/llvm/llvm-project/pull/88857
// 
// allowed to be user-specialized for custom types
template <typename T>
bool constexpr is_trivially_moveable
{
#ifdef __clang__
    __is_trivially_relocatable( T ) ||
#endif
#ifdef __cpp_lib_trivially_relocatable
    std::is_trivially_relocatable<T> ||
#endif
    std::is_trivially_move_constructible_v<T> ||
    std::is_trivially_copyable_v<T>
}; // is_trivially_moveable

#if !defined( _LIBCPP_DEBUG )
template <typename E, typename T, typename A> bool constexpr is_trivially_moveable<std::basic_string<E, T, A>>{ is_trivially_moveable<A> };
template <typename T            , typename A> bool constexpr is_trivially_moveable<std::vector      <T, A   >>{ is_trivially_moveable<A> };
template <typename T            , typename D> bool constexpr is_trivially_moveable<std::unique_ptr  <T, D   >>{ is_trivially_moveable<D> };
#endif
#if defined( __GLIBCXX__ )
template <typename S> bool constexpr is_trivially_moveable<std::function<S>>{ true };
#endif

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
