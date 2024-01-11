////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2023 - 2024.
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
#include <psi/vm/protection.hpp>

#ifdef _WIN32
#include <windows.h>
#else // POSIX
#include <sys/mman.h>
#endif // platform
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

enum protection : std::uint32_t
{
#ifdef _WIN32
    access_none = PAGE_NOACCESS,
    rd_only     = PAGE_READONLY,
    rw          = PAGE_READWRITE
#else // POSIX
    access_none = PROT_NONE,
    rd_only     = PROT_READ,
    rw          = PROT_READ | PROT_WRITE
#endif // platform
}; // enum protection

protection const no_access { protection::access_none };
protection const read_only { protection::rd_only     };
protection const read_write{ protection::rw          };

err::void_or_error< error > protect( void * const region_begin, std::size_t const region_size, std::underlying_type_t<protection> const access_flags ) noexcept
{
#ifdef _WIN32
    DWORD previous_flags;
    if ( ::VirtualProtect( region_begin, region_size, access_flags, &previous_flags ) )
#else
    if ( ::mprotect( region_begin, region_size, static_cast<int>( access_flags ) ) == 0 )
#endif
        return err::success;

    return error{};
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
