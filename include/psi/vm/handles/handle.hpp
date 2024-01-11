////////////////////////////////////////////////////////////////////////////////
///
/// \file handle.hpp
/// ----------------
///
/// Copyright (c) Domagoj Saric 2011 - 2024.
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
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------

#ifdef _MSC_VER
#   pragma warning( push )
#   pragma warning( disable : 5030 ) // Unrecognized attribute
#endif // _MSC_VER

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

    static auto const invalid_value{ traits::invalid_value };

             constexpr handle_impl(                                        ) noexcept : handle_{ traits::invalid_value } {                                        }
    explicit constexpr handle_impl( native_handle_t    const native_handle ) noexcept : handle_{ native_handle         } {                                        }
             constexpr handle_impl( handle_impl     &&       other         ) noexcept : handle_{ other.handle_         } { other.handle_ = traits::invalid_value; }
                      ~handle_impl(                                        ) noexcept                                    { traits::close( handle_ );              }

    handle_impl & operator=( handle_impl && __restrict other ) noexcept
    {
        close();
        this->handle_ = other.handle_;
        other.handle_ = traits::invalid_value;
        return *this;
    }

    void close() noexcept { traits::close( release() ); }

    native_handle_t release() noexcept
    {
        auto const result{ handle_ };
        handle_ = traits::invalid_value;
        return result;
    }

    native_handle_t get() const noexcept { return handle_; }

    explicit operator bool() const noexcept { return handle_ != traits::invalid_value; }

private:
    native_handle_t handle_;
}; // class handle_impl

#ifdef _MSC_VER
#   pragma warning( pop )
#endif // _MSC_VER

//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------
#include PSI_VM_IMPL_INCLUDE( handle )
