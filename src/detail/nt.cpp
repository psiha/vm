////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2015 - 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
/// (See accompanying file LICENSE_1_0.txt or copy at
/// http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
/// http://undocumented.ntinternals.net
/// https://www.i.u-tokyo.ac.jp/edu/training/ss/lecture/new-documents/Lectures/02-VirtualMemory/VirtualMemory.pdf
/// http://ultradefrag.sourceforge.net/doc/man/Windows%20NT(2000)%20Native%20API%20Reference.pdf
/// https://webdiis.unizar.es/~spd/pub/windows/ntdll.htm
/// https://technet.microsoft.com/en-us/sysinternals/bb896657.aspx WinObj
/// https://www.osronline.com/article.cfm?id=91
/// http://downloads.securityfocus.com/vulnerabilities/exploits/20940-Ivan.c
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include <psi/vm/detail/nt.hpp>
#include <psi/vm/handles/handle.hpp> // handle_traits::copy() definition

#include <psi/build/disable_warnings.hpp>

#include <boost/assert.hpp>

#pragma comment( lib, "ntdll.lib" )
//------------------------------------------------------------------------------
namespace psi::vm::nt
{
//------------------------------------------------------------------------------

namespace detail
{
    // initialization order fiasco wrkrnds
#if defined( __clang__ ) || defined( __GNUC__ ) // clang-cl does not define __GNUC__
    HMODULE ntdll;
    [[ gnu::constructor( 101 ) ]]
    void init_ntdll_handle() noexcept { ntdll = ::GetModuleHandleW( L"ntdll.dll" ); }
#else
    PSI_WARNING_DISABLE_PUSH()
    PSI_WARNING_MSVC_DISABLE( 4073 ) // initializers put in library initialization area
    #pragma init_seg( lib )
    HMODULE const ntdll{ ::GetModuleHandleW( L"ntdll.dll" ) };
    PSI_WARNING_DISABLE_POP()
#endif

    BOOST_ATTRIBUTES( BOOST_COLD, BOOST_RESTRICTED_FUNCTION_L3, BOOST_RESTRICTED_FUNCTION_RETURN )
    ::PROC try_get_nt_proc( char const * const proc_name ) noexcept
    {
        BOOST_ASSERT( current_process == ::GetCurrentProcess() ); // for lack of a better place for this sanity check
        BOOST_ASSUME( ntdll );
        return ::GetProcAddress( ntdll, proc_name );
    }

    BOOST_ATTRIBUTES( BOOST_COLD, BOOST_RESTRICTED_FUNCTION_L3, BOOST_RESTRICTED_FUNCTION_RETURN )
    ::PROC get_nt_proc( char const * const proc_name ) noexcept
    {
        auto const result{ try_get_nt_proc( proc_name ) };
        BOOST_ASSUME( result );
        return result;
    }
} // namespace detail

//------------------------------------------------------------------------------
} // psi::vm::nt

//------------------------------------------------------------------------------
// win32::handle_traits::copy() -- needs <windows.h> which nt.hpp provides
//------------------------------------------------------------------------------

[[ gnu::cold ]]
psi::vm::fallible_result<psi::vm::win32::handle_traits::native_t>
psi::vm::win32::handle_traits::copy( native_t const native_handle ) noexcept
{
    if ( !native_handle || native_handle == boost::winapi::INVALID_HANDLE_VALUE_ )
        return invalid_value;
    native_t dup{ nullptr };
    if ( !::DuplicateHandle
    (
        nt::current_process, native_handle,
        nt::current_process, &dup,
        0, FALSE, DUPLICATE_SAME_ACCESS
    ) ) [[ unlikely ]]
        return error{};
    return dup;
}

//------------------------------------------------------------------------------
