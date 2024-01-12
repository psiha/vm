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
#include "allocation.impl.hpp"
#include <psi/vm/detail/nt.hpp>
#include <psi/vm/detail/win32.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>

#include <algorithm> // for std::min
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

auto const this_process{ reinterpret_cast<HANDLE>( static_cast<std::intptr_t>( -1 ) ) };

enum class deallocation_type : std::uint32_t
{
    free     = MEM_RELEASE,
    decommit = MEM_DECOMMIT
};

#define NATIVE_NT 1

BOOST_NOINLINE
auto alloc( void * & desired_location, std::size_t & size, allocation_type const type ) noexcept
{
#if NATIVE_NT
    using namespace nt;
    auto const requested_location{ desired_location };
    SIZE_T sz{ size };
    auto const nt_status
    {
        NtAllocateVirtualMemory
        (
            this_process,
            reinterpret_cast< PVOID * >( &desired_location ),
            0, // address zero bits
            &sz,
            std::to_underlying( type ),
            PAGE_READWRITE
        )
    };
    if ( nt_status == STATUS_SUCCESS )
    {
        BOOST_ASSUME( desired_location == requested_location || !requested_location );
        size = static_cast< std::size_t >( sz );
    }
    else
    if ( nt_status == STATUS_CONFLICTING_ADDRESSES )
    {
        BOOST_ASSUME( sz == size );
    }
    else
    {
        BOOST_ASSUME( ( nt_status == STATUS_NO_MEMORY ) || ( nt_status == STATUS_INVALID_PARAMETER && !size ) );
        desired_location = nullptr;
    }
    return nt_status;
#else
    auto const result{ ::VirtualAlloc( desired_location, size, type, PAGE_READWRITE ) };
    if ( result )
    {
        desired_location = result;
        return true;
    }
    BOOST_ASSERT( ::GetLastError() == STATUS_NO_MEMORY );
    return false;
#endif
}

void dealloc( void * & address, std::size_t & size, deallocation_type const type ) noexcept
{
#if NATIVE_NT
    using namespace nt;
    SIZE_T sz( size );
    auto const result{ NtFreeVirtualMemory( this_process, &address, &sz, std::to_underlying( type ) ) };
    size = static_cast< std::size_t >( sz );
    BOOST_ASSUME( result == STATUS_SUCCESS || size == 0 );
#else
    BOOST_VERIFY( ::VirtualFree( address, size, type ) || !address );
#endif
}

auto mem_info( void * const pAddress ) noexcept
{
    MEMORY_BASIC_INFORMATION info;
    BOOST_VERIFY( ::VirtualQueryEx( this_process, pAddress, &info, sizeof( info ) ) == sizeof( info ) );
    BOOST_ASSUME( info.BaseAddress == pAddress );
    return info;
}

void * allocate( std::size_t & size ) noexcept { using namespace nt; void * address{ nullptr }; BOOST_VERIFY( alloc( address, size, allocation_type( std::to_underlying( allocation_type::reserve ) | std::to_underlying( allocation_type::commit ) ) ) == STATUS_SUCCESS || !address ); return address; }
void * reserve ( std::size_t & size ) noexcept { using namespace nt; void * address{ nullptr }; BOOST_VERIFY( alloc( address, size,                                      allocation_type::reserve                                                     ) == STATUS_SUCCESS || !address ); return address; }

bool commit( void * const desired_location, std::size_t const size ) noexcept
{
    using namespace nt;

    // emulate support for adjacent/concatenated regions (as supported by mmap)
    auto final_address{ desired_location };
    auto final_size   { size             };
    auto result{ alloc( final_address, final_size, allocation_type::commit ) };
    if ( result == STATUS_CONFLICTING_ADDRESSES ) [[ unlikely ]] // Windows kernel does not merge ranges/allow API calls across VM allocations
    {
        final_size = 0;
        while ( final_size != size )
        {
            auto const info{ mem_info( final_address ) };
            BOOST_ASSUME( info.AllocationProtect == PAGE_READWRITE );
            BOOST_ASSUME( info.State             == MEM_RESERVE    );
            BOOST_ASSUME( info.Protect           == 0              );
            BOOST_ASSUME( info.Type              == MEM_PRIVATE    );
            auto regionSize{ std::min( static_cast< std::size_t >( info.RegionSize ), size - final_size ) };
            auto const partial_result{ alloc( final_address, regionSize, allocation_type::commit ) };
            if ( partial_result != STATUS_SUCCESS )
            {
                BOOST_ASSERT( partial_result == STATUS_NO_MEMORY );
                return false;
            }
            BOOST_ASSUME( regionSize <= info.RegionSize );

            add( final_address, regionSize );
            add( final_size   , regionSize );
        }
        final_address = desired_location;
        result        = STATUS_SUCCESS;
    }
    auto const success{ result == STATUS_SUCCESS };
    BOOST_ASSERT( final_address == desired_location || !success );
    BOOST_ASSERT( final_size    == size             || !success );
    return success;
}

void decommit( void * const address, std::size_t const size ) noexcept
{
    auto final_address{ address };
    auto final_size   { size    };
    dealloc( final_address, final_size, deallocation_type::decommit );
    BOOST_ASSUME( final_address == address );
    BOOST_ASSUME( final_size    == size    );
}
BOOST_NOINLINE
void free( void * address, std::size_t size ) noexcept
{
    // emulate support for adjacent/concatenated regions (as supported by mmap)
    while ( size )
    {
        std::size_t releasedSize{ 0 };
        dealloc( address, releasedSize, deallocation_type::free );
        BOOST_ASSERT( releasedSize <= size );
        add( address, releasedSize );
        sub( size   , releasedSize );
    }
}

bool allocate_fixed( void * const address, std::size_t const size, allocation_type const alloc_type ) noexcept
{
    using namespace nt;

    // MEM_COMMIT does not imply MEM_RESERVE which is required for initial allocation
    // and it also ensures that we cannot overwrite an existing mapping for
    // pre-and-post allocations (MEM_COMMIT | MEM_RESERVE will fail over an
    // already reserved range)
    auto const adjusted_commit_allocation_type{ allocation_type( std::to_underlying( alloc_type ) | std::to_underlying( allocation_type::reserve ) ) };
    auto adjusted_address{ address };
    auto adjusted_size   { size    };
    auto const result{ alloc( adjusted_address, adjusted_size, adjusted_commit_allocation_type ) };
    BOOST_ASSUME( adjusted_address == address );
    BOOST_ASSUME( adjusted_size    == size    );
    return result == STATUS_SUCCESS;
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
