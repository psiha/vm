////////////////////////////////////////////////////////////////////////////////
///
/// \file handle_ref.hpp
/// --------------------
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
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

#ifdef _MSC_VER
    #pragma warning( push )
    #pragma warning( disable : 4510 ) // Default constructor was implicitly defined as deleted.
    #pragma warning( disable : 4512 ) // Assignment operator could not be generated.
    #pragma warning( disable : 5030 ) // Unrecognized attribute
#endif // _MSC_VER

template <typename Handle, bool read_only>
struct handle_ref
{
    using native_handle_t = typename Handle::native_handle_t;

    constexpr handle_ref( native_handle_t         const native ) noexcept                       : value{ native       } {}
    constexpr handle_ref( Handle          const &       handle ) noexcept requires( read_only ) : value{ handle.get() } {}
    constexpr handle_ref( Handle                &       handle ) noexcept                       : value{ handle.get() } {}
    constexpr handle_ref( Handle                &&      handle ) noexcept                       : value{ handle.get() } {}

    template <bool other_read_only>
    constexpr handle_ref( handle_ref<Handle, other_read_only> const mutable_ref ) noexcept requires( !other_read_only && read_only ) : value{ mutable_ref.value } {}

    constexpr          native_handle_t get() const noexcept requires( !read_only ) { return value; }
    constexpr operator native_handle_t    () const noexcept requires( !read_only ) { return value; }

    [[ gnu::pure ]] explicit operator bool() const noexcept { return value != Handle::invalid_value; }

    [[ gnu::pure ]] constexpr bool operator==( native_handle_t const other ) const noexcept { return value == other; }

    native_handle_t value;
}; // struct handle_ref

#ifdef _MSC_VER
    #pragma warning( pop )
#endif // _MSC_VER

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
