////////////////////////////////////////////////////////////////////////////////
///
/// \file win32/file.hpp
/// --------------------
///
/// Copyright (c) Domagoj Saric 2010 - 2024.
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
#ifndef file_hpp__FB482005_18D9_4E3B_9193_A13DBFE88F45
#define file_hpp__FB482005_18D9_4E3B_9193_A13DBFE88F45
#pragma once
//------------------------------------------------------------------------------
#include "psi/vm/detail/impl_selection.hpp"
#include "psi/vm/flags/win32/opening.hpp"
#include "psi/vm/mapping/mapping.hpp"
#include "psi/vm/mappable_objects/file/handle.hpp"
#include "psi/vm/error/error.hpp"

#include <cstddef>
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------
namespace win32
{
//------------------------------------------------------------------------------

template <typename> struct is_resizable;
template <        > struct is_resizable<file_handle> : std::true_type {};

BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 )
file_handle BOOST_CC_REG create_file( char    const * file_name, flags::opening ) noexcept;
file_handle BOOST_CC_REG create_file( wchar_t const * file_name, flags::opening ) noexcept;
bool        BOOST_CC_REG delete_file( char    const * file_name                 ) noexcept;
bool        BOOST_CC_REG delete_file( wchar_t const * file_name                 ) noexcept;


err::fallible_result<void, error> BOOST_CC_REG set_size( file_handle::reference, std::uint64_t desired_size ) noexcept;
std::uint64_t                     BOOST_CC_REG get_size( file_handle::reference                             ) noexcept;

// https://msdn.microsoft.com/en-us/library/ms810613.aspx Managing Memory-Mapped Files

mapping BOOST_CC_REG create_mapping
(
    file_handle::reference,
    flags::access_privileges::object,
    flags::access_privileges::child_process,
    flags::mapping          ::share_mode,
    std  ::size_t size
) noexcept;

//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------

#ifdef PSI_VM_HEADER_ONLY
    #include "file.inl"
#endif // PSI_VM_HEADER_ONLY

#endif // file_hpp
