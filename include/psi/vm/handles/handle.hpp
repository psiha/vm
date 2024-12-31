////////////////////////////////////////////////////////////////////////////////
///
/// \file handle.hpp
/// ----------------
///
/// Copyright (c) Domagoj Saric 2011 - 2025.
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

#include "handle_ref.hpp"

#include <psi/vm/detail/impl_selection.hpp>

#include <psi/build/disable_warnings.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

PSI_WARNING_DISABLE_PUSH()
PSI_WARNING_MSVC_DISABLE( 4067 ) // unexpected tokens following preprocessor directive (__has_builtin)
PSI_WARNING_MSVC_DISABLE( 5030 ) // unrecognized attribute

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

template <typename ImplTraits>
class [[ clang::trivial_abi ]] handle_impl
{
public:
    using traits          = ImplTraits;
    using native_handle_t = typename traits::native_t;
    using       reference = handle_ref<handle_impl, false>;
    using const_reference = handle_ref<handle_impl, true >;

    inline static auto const invalid_value{ traits::invalid_value };

             constexpr handle_impl(                                        ) noexcept : handle_{ invalid_value } {                                }
    explicit constexpr handle_impl( native_handle_t    const native_handle ) noexcept : handle_{ native_handle } {                                }
             constexpr handle_impl( handle_impl     &&       other         ) noexcept : handle_{ other.handle_ } { other.handle_ = invalid_value; }
                      ~handle_impl(                                        ) noexcept                            { traits::close( handle_ );      }

    handle_impl & operator=( handle_impl && __restrict other ) noexcept
    {
        close();
        this->handle_ = other.handle_;
        other.handle_ = invalid_value;
        return *this;
    }

    void reset( native_handle_t const new_handle ) noexcept
    {
        auto const old_handle{ handle_ };
        handle_ = new_handle;
#   if __has_builtin( __builtin_constant_p ) //...mrmlj...TODO deduplicate
        if ( __builtin_constant_p( old_handle ) && ( old_handle == invalid_value ) )
            return;
#   endif
        traits::close( old_handle );
    }

    void close() noexcept
    {
        auto const handle{ release() };
#   if __has_builtin( __builtin_constant_p )
        if ( __builtin_constant_p( handle ) && ( handle == invalid_value ) )
            return;
#   endif
        traits::close( handle );
    }

    native_handle_t release() noexcept
    {
        auto const result{ handle_ };
        handle_ = invalid_value;
        return result;
    }

    native_handle_t get() const noexcept { return handle_; }

    explicit operator bool() const noexcept { return handle_ != invalid_value; }

private:
    native_handle_t handle_;
}; // class handle_impl

PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
#include PSI_VM_IMPL_INCLUDE( handle )
