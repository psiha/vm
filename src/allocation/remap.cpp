////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2023 - 2026.
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
#include "expand_darwin.hpp"
#include "expand_linux.hpp"
#include "expand_win32.hpp"
#include <psi/vm/align.hpp>
#include <psi/vm/allocation.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>

#ifdef _WIN32
#   include <psi/vm/detail/nt.hpp>
#elif defined( __linux__ )
#   include <sys/mman.h>
#elif defined( __APPLE__ )
#   include <mach/mach_init.h>
#   include <mach/mach_vm.h>
#   include <mach/vm_statistics.h>
#   include <sys/mman.h>
#endif // win32 / linux / apple

#ifndef NDEBUG
#include <cerrno>
#endif
#include <cstddef>
#include <cstring> // memcpy
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

[[ gnu::cold ]]
expand_result expand
(
    std::byte         *       address,
    std::size_t         const current_size,
    std::size_t         const required_size_for_end_expansion,
    std::size_t         const required_size_for_front_expansion,
    std::size_t         const used_capacity,
    allocation_type     const alloc_type,
    reallocation_type   const realloc_type
) noexcept
{
    BOOST_ASSUME( address != nullptr    );
#ifndef _WIN32
    BOOST_ASSUME( address != MAP_FAILED );
#endif

    BOOST_ASSUME( current_size >  0                               ); // otherwise we should have never gotten here
    BOOST_ASSUME( current_size <  required_size_for_end_expansion );
    BOOST_ASSUME( current_size >= used_capacity                   );

    BOOST_ASSUME( is_aligned( address                          , reserve_granularity ) );
    BOOST_ASSUME( is_aligned( current_size                     , reserve_granularity ) );
    BOOST_ASSUME( is_aligned( required_size_for_end_expansion  , reserve_granularity ) );
    BOOST_ASSUME( is_aligned( required_size_for_front_expansion, reserve_granularity ) );

    BOOST_ASSUME( ( alloc_type == allocation_type::commit ) || !used_capacity );

    // First simply try to append or prepend the required extra space

    // - append
    if ( required_size_for_end_expansion )
    {
        BOOST_ASSUME( required_size_for_end_expansion > current_size );
#   if defined( __linux__ )
        // mremap requires the same protection for the entire range
        BOOST_VERIFY( ::mprotect( address, current_size, static_cast<int>( allocation_type::commit ) ) == 0 );
        if ( auto const r{ detail::linux_mremap( address, current_size, required_size_for_end_expansion, realloc_type ) } ) [[ likely ]]
        {
            auto const method{ ( realloc_type == reallocation_type::fixed ) ? expand_result::back_extended : expand_result::moved };
            return { { static_cast<std::byte *>( r.address ), required_size_for_end_expansion }, method };
        }
#   elif defined( __APPLE__ )
        if ( realloc_type == reallocation_type::moveable )
        {
            if ( auto const r{ detail::macos_expand_relocate( address, current_size, required_size_for_end_expansion ) } )
            {
                return { { static_cast<std::byte *>( r.address ), r.size }, expand_result::moved };
            }
        }
#   endif // linux mremap / apple mach_vm_remap
        auto const additional_end_size{ required_size_for_end_expansion - current_size };
#   ifdef _WIN32
        // Windows placeholder-based in-place expansion (shared implementation in expand_win32.hpp).
        if ( detail::try_placeholder_expand( address, current_size, additional_end_size, alloc_type ) )
        {
            return { { address, required_size_for_end_expansion }, expand_result::back_extended };
        }
#   endif // _WIN32 placeholder expansion
        if ( allocate_fixed( address + current_size, additional_end_size, alloc_type ) )
        {
#       if defined( __linux__ )
            BOOST_ASSERT_MSG( false, "mremap failed but an appending mmap succeeded!?" ); // behaviour investigation
#       endif
            BOOST_ASSUME( current_size + additional_end_size == required_size_for_end_expansion );
            return { { address, required_size_for_end_expansion }, expand_result::method::back_extended };
        }
    }

    // - prepend
    if ( required_size_for_front_expansion )
    {
        BOOST_ASSUME( required_size_for_front_expansion > current_size );
        auto const additional_front_size{ required_size_for_front_expansion - current_size };
        void * pre_address{ address - additional_front_size };
        if ( allocate_fixed( pre_address, additional_front_size, allocation_type::commit ) ) // avoid having a non-committed range before a committed range
        {
            BOOST_VERIFY( current_size + additional_front_size == required_size_for_front_expansion );
            return { { static_cast< std::byte * >( pre_address ), required_size_for_front_expansion }, expand_result::method::front_extended };
        }
    }

    if ( realloc_type == reallocation_type::moveable )
    {
#   ifdef _WIN32
        // Windows: over-reserve with trailing placeholder (shared implementation).
        if ( required_size_for_end_expansion )
        {
            auto * const base{ detail::overreserve_expand( required_size_for_end_expansion, alloc_type ) };
            if ( base )
            {
                std::memcpy( base, address, used_capacity );
                free( address, current_size );
                return { { base, required_size_for_end_expansion }, expand_result::moved };
            }
        }
#   endif // _WIN32 placeholder over-reserve

        // Generic fallback: allocate new->copy->free old dance.
        auto       requested_size{ required_size_for_end_expansion }; //...mrmlj...TODO respect front-expand-only requests
        auto const new_location  { allocate( requested_size )      };
        if ( new_location )
        {
            BOOST_ASSUME( requested_size == required_size_for_end_expansion );
            std::memcpy( new_location, address, used_capacity );
            free( address, current_size );
            return { { static_cast< std::byte * >( new_location ), required_size_for_end_expansion }, expand_result::method::moved };
        }
    }

    return {};
}

expand_result expand_back
(
    std::byte *       const address,
    std::size_t       const current_size,
    std::size_t       const required_size,
    std::size_t       const used_capacity,
    allocation_type   const alloc_type,
    reallocation_type const realloc_type
) noexcept
{
    return expand( address, current_size, required_size, 0, used_capacity, alloc_type, realloc_type );
}

expand_result expand_front
(
    std::byte *       const address,
    std::size_t       const current_size,
    std::size_t       const required_size,
    std::size_t       const used_capacity,
    allocation_type   const alloc_type,
    reallocation_type const realloc_type
) noexcept
{
    return expand( address, current_size, 0, required_size, used_capacity, alloc_type, realloc_type );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
