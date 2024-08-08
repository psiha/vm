////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2015 - 2024.
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

#include <boost/assert.hpp>

#pragma comment( lib, "ntdll.lib" )
//------------------------------------------------------------------------------
namespace psi::vm::nt
{
//------------------------------------------------------------------------------

namespace detail
{
#ifdef __clang__ // initialization order fiasco wrkrnd
    HMODULE ntdll;
    [[ gnu::constructor( 101 ) ]]
    void init_ntdll_handle() noexcept { ntdll = ::GetModuleHandleW( L"ntdll.dll" ); }
#else
    HMODULE const ntdll{ ::GetModuleHandleW( L"ntdll.dll" ) };
#endif

    BOOST_ATTRIBUTES( BOOST_COLD, BOOST_RESTRICTED_FUNCTION_L3, BOOST_RESTRICTED_FUNCTION_RETURN )
    ::PROC get_nt_proc( char const * const proc_name ) noexcept
    {
        BOOST_ASSERT( current_process == ::GetCurrentProcess() ); // for lack of a better place for this sanity check
        BOOST_ASSUME( ntdll );
        auto const result{ ::GetProcAddress( ntdll, proc_name ) };
        BOOST_ASSUME( result );
        return result;
    }
} // namespace detail

//------------------------------------------------------------------------------
} // psi::vm::nt
//------------------------------------------------------------------------------
