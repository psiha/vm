////////////////////////////////////////////////////////////////////////////////
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
#pragma once

#include <psi/vm/flags/mapping.hpp>
#include <psi/vm/handles/handle.hpp>
#include <psi/vm/mapping/mapping.hpp>
#include <psi/vm/span.hpp>

#include <boost/config_ex.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

struct mapper
{
    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
    mapped_span BOOST_CC_REG
    map
    (
        mapping::handle  source_mapping,
        flags ::viewing  flags         ,
        std   ::uint64_t offset        ,
        std   ::size_t   desired_size
    ) noexcept;

    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 )
    void BOOST_CC_REG unmap( mapped_span view ) noexcept;

    static void shrink( mapped_span view, std::size_t target_size ) noexcept;

    static void flush( mapped_span view ) noexcept;
}; // struct mapper

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
