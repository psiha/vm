////////////////////////////////////////////////////////////////////////////////
///
/// \file handle.hpp
/// ----------------
///
/// Copyright (c) Domagoj Saric 2011 - 2015.
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
#ifndef handle_hpp__67A36E06_53FF_4361_9786_9E04A3917CD3
#define handle_hpp__67A36E06_53FF_4361_9786_9E04A3917CD3
#pragma once
//------------------------------------------------------------------------------
#include "handle_ref.hpp"

#include "boost/mmap/detail/impl_selection.hpp"

#include BOOST_MMAP_IMPL_INCLUDE( BOOST_PP_EMPTY, BOOST_PP_IDENTITY( /handle.hpp ) )
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl>
struct handle_traits;


////////////////////////////////////////////////////////////////////////////////
///
/// \class handle
///
/// \brief Generic 'handle' RAII wrapper.
///
////////////////////////////////////////////////////////////////////////////////
/// \todo Should be moved to a separate RAII library.
///                                           (30.05.2015.) (Domagoj Saric)
////////////////////////////////////////////////////////////////////////////////

template <typename Impl>
class handle
{
public:
    using traits          = handle_traits<Impl>;
    using native_handle_t = typename traits::native_t;
    using reference       = handle_ref<handle<Impl>>;

             handle(                                        ) noexcept : handle_( traits::invalid_value ) {                                        }
    explicit handle( native_handle_t    const native_handle ) noexcept : handle_( native_handle         ) {                                        }
             handle( handle          &&       other         ) noexcept : handle_( other.handle_         ) { other.handle_ = traits::invalid_value; }
            ~handle(                                        ) noexcept                                    { traits::close( handle_ );              }

    handle && operator=( handle && BOOST_RESTRICTED_REF other ) noexcept
    {
        close();
        this->handle_ = other.handle_;
        other.handle_ = traits::invalid_value;
    }

    void close() noexcept { traits::close( release() ); }

    native_handle_t release() noexcept
    {
        auto const result( handle_ );
        handle_ = traits::invalid_value;
        return result;
    }

    native_handle_t const & get() const noexcept { return handle_; }

    explicit operator bool () const noexcept { return handle_ != traits::invalid_value; }

    operator reference () const noexcept { return reference{ this->get() }; }

private:
    native_handle_t handle_;
}; // class handle

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // handle_hpp
