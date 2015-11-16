////////////////////////////////////////////////////////////////////////////////
///
/// \file guarded_operation.hpp
/// ---------------------------
///
/// Copyright (c) Domagoj Saric 2015.
///
/// Use, modification and distribution is subject to
/// the Boost Software License, Version 1.0.
/// (See accompanying file LICENSE_1_0.txt or copy at
/// http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef guarded_operation_hpp__B181EBDC_EA6B_451A_90D0_B6E1BE57DCA8
#define guarded_operation_hpp__B181EBDC_EA6B_451A_90D0_B6E1BE57DCA8
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/mapped_view/mapped_view.hpp"

#ifdef _WIN32
#include <boost/mmap/detail/win32.hpp>

#include <excpt.h>
#else
#include "boost/assert.hpp"
#include "boost/config.hpp"

#include <csetjmp>
//#include <sys/signal.h> // requires Android NDK API 21
#include <signal.h>
#endif // _WIN32
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
//
// guarded_operation()
// -------------------
//
////////////////////////////////////////////////////////////////////////////////
///
/// \brief Executes <VAR>operation</VAR> on the <VAR>view</VAR> with platform-
/// specific guards in place that will catch access violation errors/signals/
/// exceptions (which might happen if the OS is unable to actually load a part
/// of the requested memory region/view, e.g. if the view represents a file on a
/// network drive and the network connection breaks).
///
/// \throws Whatever Operation or ErrorHandler throw
///
/// <B>POSIX specific</B>: Any calls made by <VAR>operation</VAR> have to be
/// "async signal safe".
///
////////////////////////////////////////////////////////////////////////////////

template <typename Element, class Operation, class ErrorHandler>
typename std::result_of<Operation( basic_memory_range<Element> )>::type
guarded_operation
(
    basic_memory_range<Element> const view,
    Operation                   const operation,
    ErrorHandler                const error_handler
)
{
#ifdef _WIN32
    void const * exception_location;
    __try
    {
        return operation( view );
    }
    __except
    (
        ( ::GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR )
            ? ( exception_location = (/*::*/GetExceptionInformation())->ExceptionRecord->ExceptionAddress, EXCEPTION_EXECUTE_HANDLER )
            :                                                                                              EXCEPTION_CONTINUE_SEARCH
    )
    {
        return error_handler( exception_location );
    }
#else
    static BOOST_THREAD_LOCAL_POD ::jmp_buf bailout_context;
    static BOOST_THREAD_LOCAL_POD void const * exception_location;

    int const expected_signal_type( SIGBUS /*SIGSEGV*/ );
    struct ::sigaction /*const*/ action =
    {
        // not yet supported by GCC in C++ :/
        //.sa_sigaction = ...
        //.sa_flags     = SA_SIGINFO
        {0}
    };
    action.sa_sigaction =
        []( int const signal_code, siginfo_t * __restrict const p_info, void * /*context*/ )
        {
            // http://stackoverflow.com/questions/1715413/longjmp-from-signal-handler
            BOOST_ASSERT( signal_code == expected_signal_type ); (void)signal_code;
            exception_location = p_info->si_addr;
            ::siglongjmp( bailout_context, true );
        };
    action.sa_flags = SA_SIGINFO;

    struct scope_exit
    {
        ~scope_exit() { BOOST_VERIFY( ::sigaction( expected_signal_type, &handler, nullptr ) == 0 ); }
        struct ::sigaction handler;
    } previous_handler;
    BOOST_VERIFY( ::sigaction( expected_signal_type, &action, &previous_handler.handler ) == 0 );
    if ( BOOST_UNLIKELY( ::/*sig*/setjmp( bailout_context ) ) )
    {
        return error_handler( exception_location );
    }
    return operation( view );
#endif // OS
}

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
#endif // guarded_operation_hpp
