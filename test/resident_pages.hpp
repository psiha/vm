#pragma once

#include <psi/vm/align.hpp>
#include <psi/vm/allocation.hpp>

#if defined( __linux__ )
#   include <sys/mman.h> // mincore
#elif defined( _WIN32 )
#   include <windows.h>
#   include <psapi.h>    // QueryWorkingSetEx
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// How many of the pages under 'bytes' are resident - mincore() on Linux, the
// working set on Windows - or nothing where neither can be asked.
inline std::optional<std::size_t> resident_pages( std::span<std::byte const> const bytes )
{
    auto const begin{ align_down( reinterpret_cast<std::uintptr_t>( bytes.data()                ), std::uintptr_t{ page_size } ) };
    auto const end  { align_up  ( reinterpret_cast<std::uintptr_t>( bytes.data() + bytes.size() ), std::uintptr_t{ page_size } ) };
    auto const pages{ static_cast<std::size_t>( ( end - begin ) / page_size ) };
#if defined( __linux__ )
    std::vector<unsigned char> residency( pages );
    if ( ::mincore( reinterpret_cast<void *>( begin ), end - begin, residency.data() ) != 0 )
        return std::nullopt;
    return static_cast<std::size_t>( std::ranges::count_if( residency, []( unsigned char const page ) { return page & 1; } ) );
#elif defined( _WIN32 )
    std::vector<PSAPI_WORKING_SET_EX_INFORMATION> residency( pages );
    for ( std::size_t p{ 0 }; p < pages; ++p )
        residency[ p ].VirtualAddress = reinterpret_cast<void *>( begin + p * page_size );
    if ( !::QueryWorkingSetEx( ::GetCurrentProcess(), residency.data(), static_cast<DWORD>( pages * sizeof( residency[ 0 ] ) ) ) )
        return std::nullopt;
    return static_cast<std::size_t>( std::ranges::count_if( residency, []( auto const & page ) { return page.VirtualAttributes.Valid != 0; } ) );
#else
    static_cast<void>( pages );
    return std::nullopt;
#endif
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
