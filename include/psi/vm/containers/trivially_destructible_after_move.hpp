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
// Forward declarations for stdlib specializations
#ifdef _MSC_VER
namespace std { template <class T, class A> class list; }
#endif
#ifdef __GLIBCXX__
namespace std
{
_GLIBCXX_BEGIN_NAMESPACE_VERSION
#if _GLIBCXX_USE_CXX11_ABI
inline _GLIBCXX_BEGIN_NAMESPACE_CXX11
    template <class E, class T, class A> class basic_string;
_GLIBCXX_END_NAMESPACE_CXX11
#else
    template <class E, class T, class A> class basic_string;
#endif
_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std
#endif
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// Try to avoid calls to destructors of empty objects (knowing that even latest
// compilers sometimes fail to elide all calls to free/delete even w/ inlined
// destructors and move constructors in contexts where it is 'obvious' the obj
// is moved out of prior to destruction) - 'absolutely strictly' this would
// require a new, separate type trait. Absent that, the closest thing would be
// to detect the existence of an actual move constructor/assignment and assume
// these leave the object in an 'empty'/'free'/'destructed' state (this may be
// true for the majority of types but does not hold in general - e.g. for node
// based containers that allocate the sentinel node on the heap - as in other
// similar cases in the, fast-moving/WiP, containers part of the library we
// opt for an 'unsafe heuristic compromise' at this stage of development -
// catch performance early and rely on sanitizers, tests and asserts to catch
// bugs/types that need special care). There is no standardized way to detect
// even this so for starters settle for nothrow-move-assignability (assuming
// it implies a nothrow, therefore no allocation, copy if there is no
// actual/separate move constructor/assignment), adding trivial destructibility
// to encompass the stray contrived type that might not be included otherwise.
// https://github.com/psiha/vm/pull/34#discussion_r1909052203
// https://quuxplusone.github.io/blog/2025/01/10/trivially-destructible-after-move
// (crossing ways with
//   https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2839r0.html Nontrivial Relocation via a New owning reference Type
//   https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p1029r3.pdf  move = bitcopies
// )
// TODO impl https://stackoverflow.com/questions/51901837/how-to-get-if-a-type-is-truly-move-constructible/51912859#51912859

// After a move-assignment (or move-construction), can the moved-from husk's
// destructor be elided? True when:
//   (a) T is trivially destructible (dtor is always a no-op), OR
//   (b) the nothrow move guarantee "probably" implies the source is left in a
//       state whose destruction is trivial (null pointer, zero size, etc.)
// Override for types that violate (b) — yes, here we rely fully on a warrant.
//
template <typename T>
constexpr bool trivially_destructible_after_move_assignment{ std::is_nothrow_move_assignable_v<T> || std::is_trivially_destructible_v<T> };

#ifdef _MSC_VER // assuming this implies MS STL
template <typename T, typename A>
constexpr bool trivially_destructible_after_move_assignment<std::list<T, A>>{ false }; // retains a heap allocated sentinel node
#endif
#ifdef __GLIBCXX__
// libstdc++'s string ('gets' and) retains the allocator and storage from the moved-to string
// https://gcc.gnu.org/git/?p=gcc.git;a=blobdiff;f=libstdc%2B%2B-v3/include/bits/basic_string.h;h=c81dc0d425a0ae648c46f520b603971978413281;hp=aa018262c98b6633bc347bfa75492fce38f6c631;hb=540a22d243966d1b882db26b17fe674467e2a169;hpb=49e52115b09b477382fef6f04fd7b4d1641f902c
template <typename E, typename T, typename A>
bool constexpr trivially_destructible_after_move_assignment<std::basic_string<E, T, A>>{ false };
#endif

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
