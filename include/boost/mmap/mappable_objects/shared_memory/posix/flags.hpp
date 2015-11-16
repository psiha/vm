////////////////////////////////////////////////////////////////////////////////
///
/// \file shared_memory/posix/flags.hpp
/// -----------------------------------
///
/// Copyright (c) Domagoj Saric 2010 - 2015.
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
#ifndef flags_hpp__F9CD9C91_1F07_4107_A422_0D814F0FE487
#define flags_hpp__F9CD9C91_1F07_4107_A422_0D814F0FE487
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/detail/impl_selection.hpp"
#include "boost/mmap/detail/posix.hpp"
#include "boost/mmap/flags/posix/mapping.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
BOOST_MMAP_POSIX_INLINE
namespace posix
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

using flags_t = int;

struct shared_memory
{
    struct system_hints
    {
        /// \note The "only_reserve_address_space" flag has different semantics
        /// on POSIX systems (as opposed to Windows): the mapped reagion can
        /// actually be immediately accessed - if there are enough free physical
        /// memory pages - otherwise we get a SIGSEGV which can luckly be
        /// 'caught':
        /// http://stackoverflow.com/questions/3012237/handle-sigsegv-in-linux
        /// (todo: use this to implement resizable views).
        ///                                   (18.05.2015.) (Domagoj Saric)
        enum value_type
        {
            default_                   = 0,
            only_reserve_address_space = MAP_NORESERVE
        };
        value_type value;
    };

    static shared_memory BOOST_CC_REG create
    (
        access_privileges               ,
        named_object_construction_policy,
        system_hints
    ) noexcept;

    operator mapping () const noexcept
    {
        auto flags( mapping::create( ap.object_access, viewing::share_mode::shared ) );
        flags.flags |= hints.value;
        return static_cast<mapping &>( flags );
    }

    system_hints                     hints;
    access_privileges                ap;
    named_object_construction_policy nocp;
}; // struct shared_memory

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "flags.inl"
#endif // BOOST_MMAP_HEADER_ONLY

#endif // flags.hpp
