////////////////////////////////////////////////////////////////////////////////
/// psi::vm::sort_keys_radix -- the radix algorithm of psi/vm/sort_keys.hpp.
///
/// key_sort_algo::radix sorts with Boost.Sort's spreadsort::integer_sort,
/// whose header reaches into Boost.Range (and through it Boost.MPL and
/// Boost.Iterator). It is a header of its own so that a program that sorts
/// only with pdq includes none of that; one that asks for radix without
/// including this header does not compile.
///
/// PSI_VM_HAS_INTEGER_SORT says whether spreadsort's headers were found, and
/// with them whether radix is available.
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

#include "sort_keys.hpp"

#include <psi/build/disable_warnings.hpp>

#if __has_include( <boost/sort/spreadsort/integer_sort.hpp> )
#include <boost/sort/spreadsort/integer_sort.hpp>
#define PSI_VM_HAS_INTEGER_SORT 1
#else
#define PSI_VM_HAS_INTEGER_SORT 0
#endif

#include <concepts>
//------------------------------------------------------------------------------
namespace psi::vm::key_sort_detail
{
//------------------------------------------------------------------------------

PSI_WARNING_DISABLE_PUSH()
PSI_WARNING_MSVC_DISABLE( 5030 ) // unrecognized attribute

#if PSI_VM_HAS_INTEGER_SORT
// spreadsort::integer_sort allocates its bins on every call, so the worker
// that calls it cannot fail only where an allocation cannot (see
// key_sort_nothrow).
template <>
struct algorithm<key_sort_algo::radix>
{
    template <std::unsigned_integral U>
    [[ gnu::always_inline ]] static void sort( U * const first, U * const last ) { boost::sort::spreadsort::integer_sort( first, last ); }
};
#endif

PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // namespace psi::vm::key_sort_detail
//------------------------------------------------------------------------------
