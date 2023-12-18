////////////////////////////////////////////////////////////////////////////////
///
/// \file mappable_objects/file/posix/file.hpp
/// ------------------------------------------
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
#ifndef file_hpp__1E2F9841_1C6C_40D9_9AA7_BAC0003CD909
#define file_hpp__1E2F9841_1C6C_40D9_9AA7_BAC0003CD909
#pragma once
//------------------------------------------------------------------------------
#include "psi/vm/detail/impl_selection.hpp"
#include "psi/vm/detail/posix.hpp"
#include "psi/vm/flags/posix/opening.hpp"
#include "psi/vm/mappable_objects/file/handle.hpp"

#include <cstddef>
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------
PSI_VM_POSIX_INLINE
namespace posix
{
//------------------------------------------------------------------------------

template <typename> struct is_resizable;
#ifdef BOOST_HAS_UNISTD_H
    template <> struct is_resizable<handle> : std::true_type  {};
#else
    template <> struct is_resizable<handle> : std::false_type {};
#endif // BOOST_HAS_UNISTD_H


file_handle BOOST_CC_REG create_file( char    const * file_name, flags::opening ) noexcept;
#ifdef BOOST_MSVC
file_handle BOOST_CC_REG create_file( wchar_t const * file_name, flags::opening ) noexcept;
#endif // BOOST_MSVC

bool BOOST_CC_REG delete_file( char    const * path ) noexcept;
bool BOOST_CC_REG delete_file( wchar_t const * path ) noexcept;


#ifdef BOOST_HAS_UNISTD_H
err::fallible_result<void, error> BOOST_CC_REG set_size( file_handle::reference      , std::uint64_t desired_size ) noexcept;
#endif // BOOST_HAS_UNISTD_H
std::uint64_t                     BOOST_CC_REG get_size( file_handle::const_reference                             ) noexcept;


#ifdef BOOST_HAS_UNISTD_H
template <typename Handle>
mapping BOOST_CC_REG create_mapping
(
    Handle                                  &&       file,
    flags::access_privileges::object           const object_access,
    flags::access_privileges::child_process    const child_access,
    flags::mapping          ::share_mode       const share_mode,
    std::size_t                                const size
) noexcept
{
    // Apple guidelines http://developer.apple.com/library/mac/#documentation/Performance/Conceptual/FileSystem/Articles/MappingFiles.html
    (void)child_access; //...mrmlj...figure out what to do with this...
    return { std::forward<Handle>( file ), flags::viewing::create( object_access, share_mode ), size };
}
#endif // BOOST_HAS_UNISTD_H

//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------

#ifdef PSI_VM_HEADER_ONLY
#   include "file.inl"
#endif // PSI_VM_HEADER_ONLY

#endif // file_hpp
