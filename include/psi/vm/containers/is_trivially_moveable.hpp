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
#if defined( _LIBCPP_VERSION )
inline namespace __1 {
#endif
    template <typename T, size_t size> class array;
    template <typename T, typename A> class vector;
#if !defined( __GLIBCXX__ )
    template <class E, class T, class A> class basic_string;
#endif
    template <class T, class D> class unique_ptr;
    template <typename S> class function;
    template <class T1, class T2> struct pair;
#if defined( _LIBCPP_VERSION )
} // namespace __1
#endif
#if defined( __GLIBCXX__ )
inline namespace __cxx11 { template <class E, class T, class A> class basic_string; }
#endif
} // namespace std
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// template <typename T>
// bool is_trivially_moveable;
//
// Containers like tr_vector (relying on realloc) and vm_vector (relying on
// mremap) need a trait to detect/constraint on types actually supporting
// 'bitwise moves'.
// There already exist several proposals related to this (most notably P1144 and
// P2786). This library/the author leans toward 'trivially relocatable' to mean
// that an object can be simply 'paused' (i.e. not a thread safe operation),
// 'picked up' from its current location, 'dropped' at a different location and
// simply 'resumed' like nothing happened (in the sense of program correctness),
// IOW an obj can have the value of its this pointer changed w/o violating any
// of its invariants or preconditions. The semantics of P2786 sound closest to
// that - i.e. the desired behaviour for the typical example of tuple<T&> is
// that no T assignment happens - rather the tuple/obj w/ references gets
// replanted/reseated at a different location ('simply resumes its life
// unchanged at a different location').
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
// 'trivially_destructible_after_move' (see vector_impl.hpp) and a trait
// that would signal a type that contains no absolute pointers or references
// (so that it can be trivially persisted to disk or used for IPC, see
// 'does_not_hold_addresses' in vm_vector.hpp).

// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p1144r12.html std::is_trivially_relocatable
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2786r11.html Trivial Relocatability
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2959r0.html  Relocation Within Containers
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3055r1.html  Relax wording to permit relocation optimizations in the STL
// https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2014/n4158.pdf     Destructive Move
// https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2016/p0023r0.pdf   Relocator: Efficiently moving objects
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2785r3.html  Relocating prvalues
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1029r0.pdf   [[move_relocates]]
// https://gcc.gnu.org/onlinedocs/gcc-11.4.0/libstdc++/api/a06570.html __is_location_invariant
// https://quuxplusone.github.io/blog/2019/02/20/p1144-what-types-are-relocatable
// https://quuxplusone.github.io/blog/2018/09/28/trivially-relocatable-vs-destructive-movable
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
    std::is_trivially_copyable_v<T> || // implies trivial destructibility https://eel.is/c++draft/class.prop#1
    std::is_trivially_move_assignable_v<T> ||
    std::is_trivially_move_constructible_v<T> // beyond P2786 optimistic heuristic https://github.com/psiha/vm/pull/34#discussion_r1914536293
}; // is_trivially_moveable

template <typename T>
requires requires{ T::is_trivially_moveable; }
bool constexpr is_trivially_moveable<T>{ T::is_trivially_moveable };

template <typename T1, typename T2>
bool constexpr is_trivially_moveable<std::pair<T1, T2>>{ is_trivially_moveable<T1> && is_trivially_moveable<T2> };
template <typename T, std::size_t size>
bool constexpr is_trivially_moveable<std::array<T, size>>{ is_trivially_moveable<T> };
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
