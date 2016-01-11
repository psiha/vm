////////////////////////////////////////////////////////////////////////////////
///
/// \file guarded_operation.hpp
/// ---------------------------
///
/// Copyright (c) Domagoj Saric 2015 - 2016.
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

#include "boost/err/detail/thread_singleton.hpp"

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

#ifndef _WIN32
namespace details
{
    struct bailout_context
    {
        ::jmp_buf    jump_buffer;
        void const * exception_location;
    }; // struct bailout_context

    using local_bailout_context = err::detail::thread_singleton<bailout_context>;

    struct ::sigaction const handler
    (([](){
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
                // "Use of a mapped region can result in these signals:
                // SIGSEGV, SIGBUS.
                // http://man7.org/linux/man-pages/man2/mmap.2.html
                // http://stackoverflow.com/questions/1715413/longjmp-from-signal-handler
                BOOST_ASSERT( signal_code == SIGSEGV || signal_code == SIGBUS );
                auto & context( local_bailout_context::instance() );
                context.exception_location = p_info->si_addr;
                ::siglongjmp( context.jump_buffer, true );
            };
        action.sa_flags = SA_SIGINFO;
        return action;
    })());

    class scoped_signal_handler
    {
    public:
         scoped_signal_handler( int const signal_type ) noexcept : signal_type_( signal_type ) { BOOST_VERIFY( ::sigaction( signal_type_, &handler         , &original_handler_ ) == 0 ); }
        ~scoped_signal_handler(                       ) noexcept                               { BOOST_VERIFY( ::sigaction( signal_type_, &original_handler_, nullptr           ) == 0 ); }

    private:
        scoped_signal_handler( scoped_signal_handler const & ) = delete;
        int const signal_type_;
        struct ::sigaction original_handler_;
    };
} // namespace details
#endif // !_WIN32

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
    details::scoped_signal_handler const handler0( SIGSEGV );
    details::scoped_signal_handler const handler1( SIGBUS  );
    auto & context( details::local_bailout_context::instance() );
    if ( BOOST_UNLIKELY( ::sigsetjmp( context.jump_buffer, 0 ) ) )
        return error_handler( context.exception_location );
    return operation( view );
#endif // OS
}

//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
#endif // guarded_operation_hpp
