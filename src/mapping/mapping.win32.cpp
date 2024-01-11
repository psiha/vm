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
#include <psi/vm/mapping/mapping.hpp>

//#include <boost/winapi/system.hpp> //...broken?
#include <psi/vm/detail/nt.hpp>
#include <psi/vm/detail/win32.hpp>

#include <boost/assert.hpp>

#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace win32
{
//------------------------------------------------------------------------------

std::uint64_t get_size( mapping::const_handle const mapping_handle ) noexcept
{
    using namespace nt;
    SECTION_BASIC_INFORMATION info;
    auto const result{ NtQuerySection( mapping_handle.value, SECTION_INFORMATION_CLASS::SectionBasicInformation, &info, sizeof( info ), nullptr ) };
    BOOST_VERIFY( NT_SUCCESS( result ) );
    return info.SectionSize.QuadPart;
}

err::fallible_result<void, nt::error> set_size( mapping::handle const mapping_handle, std::uint64_t const new_size ) noexcept
{
    using namespace nt;
    LARGE_INTEGER ntsz{ .QuadPart = static_cast<LONGLONG>( new_size ) };
    auto const result{ NtExtendSection( mapping_handle.value, &ntsz ) };
    if ( !NT_SUCCESS( result ) ) [[ unlikely ]]
        return result;

    BOOST_ASSERT( ntsz.QuadPart >= static_cast<LONGLONG>( new_size ) );
    if ( ntsz.QuadPart > static_cast<LONGLONG>( new_size ) )
    {
        BOOST_ASSERT( ntsz.QuadPart == static_cast<LONGLONG>( get_size( mapping_handle ) ) );
        // NtExtendSection does not seem to support downsizing. TODO workaround
    }
    return err::success;
}

//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
