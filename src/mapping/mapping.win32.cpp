////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2010 - 2026.
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
#include <psi/vm/mappable_objects/file/file.win32.hpp>

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
[[ gnu::cold, gnu::noinline ]]
std::uint64_t get_size( mapping::const_handle const mapping_handle ) noexcept
{
    using namespace nt;
    SECTION_BASIC_INFORMATION info;
    info.SectionSize.QuadPart = 0; // simplify user code: return zero for closed/default constructed handles
    auto const result{ NtQuerySection( mapping_handle.value, SECTION_INFORMATION_CLASS::SectionBasicInformation, &info, sizeof( info ), nullptr ) };
    BOOST_ASSUME( result == STATUS_SUCCESS || !mapping_handle );
    return info.SectionSize.QuadPart;
}

namespace detail::create_mapping_impl
{
    HANDLE map_file( file_handle::reference file, flags::mapping::access_rights::object, flags::flags_t, std::uint64_t size ) noexcept;
    extern std::size_t const max_anonymous_pf_mapping_size;
}

err::fallible_result<void, nt::error> set_size( mapping & the_mapping, std::uint64_t const new_size ) noexcept
{
    using namespace nt;

    if ( the_mapping.is_file_based() )
    {
        LARGE_INTEGER ntsz{ .QuadPart = static_cast<LONGLONG>( new_size ) };
        auto const result{ NtExtendSection( the_mapping.get(), &ntsz ) };
        if ( !NT_SUCCESS( result ) ) [[ unlikely ]]
            return result;

        BOOST_ASSERT( ntsz.QuadPart >= static_cast<LONGLONG>( new_size ) );
        if ( ntsz.QuadPart > static_cast<LONGLONG>( new_size ) ) [[ unlikely ]]
        {
            // NtExtendSection does not support downsizing - can at least be used as a size getter (avoid get_size call)
            BOOST_ASSERT( ntsz.QuadPart == static_cast<LONGLONG>( get_size( the_mapping ) ) );
            auto const underlying_file{ the_mapping.underlying_file() };
            win32::handle & mapping_handle{ the_mapping };
            mapping_handle.close(); // no strong guarantee :/
            auto const file_resize_result{ set_size( underlying_file, new_size )() };
            if ( !file_resize_result )
                return nt::error/*...mrmlj...*/( file_resize_result.error().get() ); // TODO fully move to NativeNT API https://cpp.hotexamples.com/examples/-/-/NtSetInformationFile/cpp-ntsetinformationfile-function-examples.html
            auto const new_mapping_handle{ detail::create_mapping_impl::map_file( underlying_file, the_mapping.ap, the_mapping.view_mapping_flags.page_protection, new_size ) };
            mapping_handle.reset( new_mapping_handle );
            if ( !the_mapping )
                return nt::error/*...mrmlj...*/( err::last_win32_error::get() );
        }
    }
    else
    {
        BOOST_ASSERT( get_size( the_mapping ) == detail::create_mapping_impl::max_anonymous_pf_mapping_size );
        if ( new_size > detail::create_mapping_impl::max_anonymous_pf_mapping_size ) [[ unlikely ]]
            return nt::STATUS_SECTION_NOT_EXTENDED;
    }
    return err::success;
}

//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
