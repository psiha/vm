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
    template <class T1, class T2> struct pair;
#ifdef _LIBCPP_VERSION
} // namespace __1
#endif
} // namespace std
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// Containers like relvector (relying on realloc) and vm_vector (relying on
// mremap) need a trait to detect/constraint on types actually supporting this.
// There are actually several existing proposals related to this (most notably
// P1144 and P2786). This library/the author leans toward 'trivially
// relocatable' to mean that an object can be simply 'paused' (i.e. not a thread
// safe operation), 'picked up' from its current location, 'droped' at a
// different location and simply 'resumed' like nothing happened (in the sense
// of program correctness), IOW an obj can have the value of its this pointer
// changed w/o violating any of its invariants or preconditions. The semantics
// of P2786 sound closest to that - i.e. the desired behaviour for the typical
// example of tuple<T&> is that no T assignment happens - rather the tuple/obj
// w/ references gets replanted/reseated at a different location ('simply
// resumes its life unchanged on a different location').
// With this library already trying to 'push the envelope' on several fronts it
// does not, in this context, prioritize compatibility or safety - rather the
// goal is to provide trivial relocatability support for the widest scope of
// users/types/compilers (e.g. by using any builtin support, be it P1144 or
// P2786) - a tracer bullet pattern/strategy - there by moving the conversation
// further by providing more use cases and allowing the users to specialize the
// is_trivially_moveable value for a particular type and/or compiler where the
// OOBE is wrong/buggy.
// @ https://github.com/psiha/vm/issues/31
//
// In addition, this library needs a trait like
// 'moved_out_value_is_trivially_destructible' (see vector_impl.hpp) and a trait
// that would signal a type that contains no absolute pointers or references
// (so that it can be trivially persisted to disk or used for IPC) - this one is
// as of yet fully MIA and is required by vm_vector when using file backed
// storage (until such a thingy is devised the inadequate is_trivially_moveable
// is used).

// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p1144r12.html std::is_trivially_relocatable
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2786r11.html Trivial Relocatability
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2959r0.html Relocation Within Containers
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3055r1.html Relax wording to permit relocation optimizations in the STL
// https://quuxplusone.github.io/blog/2019/02/20/p1144-what-types-are-relocatable
// https://github.com/Quuxplusone/libcxx/blob/trivially-relocatable/test/libcxx/type_traits/is_trivially_relocatable.pass.cpp
// https://brevzin.github.io/c++/2024/10/21/trivial-relocation
// https://github.com/abseil/abseil-cpp/pull/1625
// https://reviews.llvm.org/D114732 Mark `trivial_abi` types as "trivially relocatable"
// https://github.com/llvm/llvm-project/pull/88857 Handle trivial_abi attribute for Microsoft ABI
// https://github.com/llvm/llvm-project/issues/69394 is_trivially_relocatable isn't correct under Windows
// https://github.com/llvm/llvm-project/issues/86354 is_trivially_relocatable isn't correct under Apple Clang

// allowed/expected to be user-specialized for custom types
template <typename T>
bool constexpr is_trivially_moveable
{
#ifdef __clang__
    __is_trivially_relocatable( T ) ||
#endif
#if defined( __cpp_lib_trivially_relocatable /*P1144*/ ) || defined( __cpp_trivial_relocatability /*P2786*/ )
    std::is_trivially_relocatable<T> ||
#endif
    std::is_trivially_move_assignable_v<T> ||
    // contrived types support below this line
    std::is_trivially_move_constructible_v<T> ||
    ( std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T> )
}; // is_trivially_moveable

template <typename T1, typename T2>
bool constexpr is_trivially_moveable<std::pair<T1, T2>>{ is_trivially_moveable<T1> && is_trivially_moveable<T2> };
#if !defined( _LIBCPP_DEBUG )
template <typename T            , typename A> bool constexpr is_trivially_moveable<std::vector      <T, A   >>{ is_trivially_moveable<A> };
template <typename T            , typename D> bool constexpr is_trivially_moveable<std::unique_ptr  <T, D   >>{ is_trivially_moveable<D> };
#endif
#if !defined( _LIBCPP_DEBUG ) && !defined( __GLIBCXX__ )
template <typename E, typename T, typename A> bool constexpr is_trivially_moveable<std::basic_string<E, T, A>>{ is_trivially_moveable<A> };
#endif
#if defined( __GLIBCXX__ )
template <typename S> bool constexpr is_trivially_moveable<std::function<S>>{ true };
#endif

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
