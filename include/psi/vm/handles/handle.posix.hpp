////////////////////////////////////////////////////////////////////////////////
///
/// \file handle.hpp
/// ----------------
///
/// Copyright (c) Domagoj Saric 2010 - 2025.
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

#include <psi/vm/handles/handle.hpp>
#include <psi/err/errno.hpp>
#include <psi/err/exceptions.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>

#include <psi/build/disable_warnings.hpp>

#ifdef _MSC_VER
#   pragma warning( disable : 4996 ) // "The POSIX name for this item is deprecated. Instead, use the ISO C++ conformant name."
#   include <io.h>
#else
#   include <unistd.h>
#endif
#include <fcntl.h>

#include <cerrno>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
PSI_VM_POSIX_INLINE
namespace posix
{
//------------------------------------------------------------------------------

PSI_WARNING_DISABLE_PUSH()
PSI_WARNING_MSVC_DISABLE( 4067 ) // unexpected tokens following preprocessor directive (__has_builtin)
PSI_WARNING_MSVC_DISABLE( 5030 ) // unrecognized attribute
PSI_WARNING_MSVC_DISABLE( 5223 ) // all attribute names in the attribute namespace 'msvc' are reserved for the implementation
PSI_WARNING_CLANG_DISABLE( -Wunknown-attributes )

struct handle_traits
{
    using native_t = int;

    static native_t constexpr invalid_value = -1;

    // Wkrnd attempt for GCC and Clang emitting noexcept-std::terminate handlers
    // for simple C function wrappers (i.e. lacking MSVC's "assume extern C
    // functions do not throw" option/functionality)
    // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=86286
    // https://discourse.llvm.org/t/rfc-clang-true-noexcept-aka-defaults-are-often-wrong-hardcoded-defaults-are-always-wrong/67629/15?page=2
    // https://logan.tw/llvm/nounwind.html
    [[ gnu::cold, gnu::nothrow, msvc::noalias, msvc::nothrow, clang::nouwtable ]]
    static void close( native_t const native_handle )
    {
        // The invalid handle has to be rejected at runtime, not merely when the
        // compiler happens to be able to prove the value (which it practically
        // never can for a handle loaded from an object member): destroying a
        // default-constructed or moved-from handle is an ordinary and frequent
        // event, and without this check each one issues a close( -1 ) that fails
        // with EBADF. Besides the wasted syscalls that also forced the assertion
        // below to whitelist a failure, which in turn hid genuine close()
        // failures on valid handles.
        if ( native_handle == invalid_value )
            return;
        [[ maybe_unused ]] auto const close_result{ ::close( native_handle ) };
        BOOST_ASSERT( close_result == 0 );
    }

    // Throws on failure. Cannot use fallible_result<int, last_errno> because
    // result_or_error's template constructors are ambiguous when both Result
    // and Error are constructible from int (native_t = int on POSIX).
    [[ gnu::cold ]]
    static native_t copy( native_t const native_handle )
    {
        if ( native_handle == invalid_value )
            return invalid_value;
        auto const result{ ::dup( native_handle ) };
        if ( result == invalid_value ) [[ unlikely ]]
            err::make_and_throw_exception( err::last_errno{} );
        return result;
    }
}; // handle_traits

PSI_WARNING_DISABLE_POP()

using handle = handle_impl<handle_traits>;

//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
