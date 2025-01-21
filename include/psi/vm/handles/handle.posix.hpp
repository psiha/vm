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
#   if __has_builtin( __builtin_constant_p )
        if ( __builtin_constant_p( native_handle ) && ( native_handle == invalid_value ) )
            return;
#   endif
        [[ maybe_unused ]] auto const close_result{ ::close( native_handle ) };
        BOOST_ASSERT
        (
            ( close_result == 0 ) ||
            (
                ( native_handle == invalid_value ) &&
                ( errno         == EBADF         )
            )
        );
    }

    static native_t copy( native_t native_handle ); // TODO
}; // handle_traits

PSI_WARNING_DISABLE_POP()

using handle = handle_impl<handle_traits>;

//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
