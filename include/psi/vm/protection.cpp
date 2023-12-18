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
#include "protection.hpp"

#ifdef _WIN32
#include <windows.h>
#else // POSIX
#include <sys/mman.h>
#endif // platform
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------

enum protection : std::uint32_t
{
#ifdef _WIN32
    no_access  = PAGE_NOACCESS,
    read_only  = PAGE_READONLY,
    read_write = PAGE_READWRITE
#else // POSIX
    no_access  = PROT_NONE,
    read_only  = PROT_READ,
    read_write = PROT_READ | PROT_WRITE
#endif // platform
};

protection const no_access { protection::no_access  };
protection const read_only { protection::read_only  };
protection const read_write{ protection::read_write };

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
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------
