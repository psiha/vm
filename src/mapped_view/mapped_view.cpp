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
#include "mapped_view.win32.hpp"
#include "../allocation/expand_darwin.hpp"
#include "../allocation/expand_linux.hpp"
#include "../allocation/expand_win32.hpp"

#include <psi/vm/align.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>

#include <cstring> // memcpy, memcmp
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// implemented in impl cpps
BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS )
mapped_span
map
(
    mapping::handle  source_mapping,
    flags ::viewing  flags         ,
    std   ::uint64_t offset        ,
    std   ::size_t   desired_size  ,
    bool             file_backed
) noexcept;

BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 )
void unmap( mapped_span view );

void unmap_partial( mapped_span range ) noexcept;
//------------------------------------------------------------------------------


template <bool read_only>
BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
err::fallible_result<basic_mapped_view<read_only>, error>
basic_mapped_view<read_only>::map
(
    mapping        &       source_mapping,
    flags::viewing   const flags,
    std::uint64_t    const offset,
    std::size_t      const desired_size
) noexcept
{
    auto const mapped_span{ vm::map( source_mapping, flags, offset, desired_size, source_mapping.is_file_based() ) };
    if ( mapped_span.size() != desired_size ) [[ unlikely ]]
    {
        BOOST_ASSERT( mapped_span.empty() );
        return error_t{};
    }
    return basic_mapped_view( mapped_span );
}


template <bool read_only>
void basic_mapped_view<read_only>::do_unmap() noexcept
{
    vm::unmap( reinterpret_cast<mapped_span const &>( static_cast<span const &>( *this ) ) );
}


namespace
{
    void do_shrink( mapped_span const view, std::size_t const target_size ) noexcept
    {
        // avoid syscalls
        // TODO:
        //  - check&document userland wrapper (munmap, UnmapViewOfFile...) behaviours WRT this on all targets
        //  - reconsider where to put this check
        if ( view.size() != target_size ) {
            unmap_partial
            ({
                align_up  ( view.data() + target_size, commit_granularity ),
                align_down( view.size() - target_size, commit_granularity )
            });
        }
    }
} // anonymous namespace

template <bool read_only>
void basic_mapped_view<read_only>::shrink( std::size_t const target_size ) noexcept
{
    do_shrink( reinterpret_cast<mapped_span const &>( static_cast<span const &>( *this ) ), target_size );
    static_cast<span &>( *this ) = { this->data(), target_size };
}

#ifdef _WIN32
std::size_t mem_region_size( void * const address ) noexcept;
#endif

template <bool read_only> BOOST_NOINLINE
fallible_result<void>
basic_mapped_view<read_only>::expand( std::size_t const target_size, mapping & original_mapping ) noexcept
{
    auto const current_address    { const_cast<std::byte *>( this->data() ) };
    auto const current_size       {                          this->size()   };
    auto const kernel_current_size{ align_up( current_size, reserve_granularity ) };

    // Fast path: requested size fits within the already-reserved region.
    if ( kernel_current_size >= target_size ) [[ likely ]]
    {
        static_cast<span &>( *this ) = { current_address, target_size };
        return err::success;
    }

    //--------------------------------------------------------------------------
    // Platform-specific growth.
    // Each platform has its own optimal strategy:
    //   Linux:  mremap (atomic, relocating, COW-safe)
    //   macOS:  adjacent mmap hint → mach_vm_remap fallback
    //   Windows: adjacent section view → new section view fallback
    //--------------------------------------------------------------------------

#if defined( __linux__ )
    // unlike realloc mremap does not support functioning (also) as 'malloc'
    // i.e. it does not support nullptr as the 'old address'
    if ( !current_address ) [[ unlikely ]] // initial mapping
    {
        BOOST_ASSUME( !current_size );
        auto initial_address
        {
            posix::mmap
            (
                nullptr,
                target_size,
                original_mapping.view_mapping_flags.protection,
                original_mapping.view_mapping_flags.flags,
                original_mapping.get(),
                0
            )
        };
        if ( initial_address ) [[ likely ]]
        {
            static_cast<span &>( *this ) = { static_cast<typename span::pointer>( initial_address ), target_size };
            return err::success;
        }
        return error_t{};
    }

    // mremap handles in-place extension, relocation, and COW preservation.
    if ( auto const r{ detail::linux_mremap( current_address, current_size, target_size ) } ) [[ likely ]]
    {
        static_cast<span &>( *this ) = { static_cast<typename span::pointer>( r.address ), target_size };
        return err::success;
    }
    return error_t{};

#else // Windows + macOS: try adjacent mapping, then fallback

    auto const additional_tail_size{ target_size - kernel_current_size };
    auto const tail_target_address { current_address + kernel_current_size };

    // Step 1a: try placeholder-based section expansion (fast path when a
    // trailing placeholder exists from a previous overreserve operation).
#   ifdef _WIN32
    if ( current_address )
    {
        auto const aligned_additional{ align_up( additional_tail_size, reserve_granularity ) };
        if
        (
            detail::try_placeholder_section_expand
            (
                original_mapping.get(),
                current_address,
                kernel_current_size,
                aligned_additional,
                original_mapping.view_mapping_flags.page_protection
            )
        )
        {
            static_cast<span &>( *this ) = { current_address, target_size };
            return err::success;
        }
    }

    // Step 1b: try to extend in-place by mapping additional pages adjacent.
    auto const new_address
    {
        windows_mmap
        (
            original_mapping,
            tail_target_address,
            additional_tail_size,
            kernel_current_size, // offset into section
            original_mapping.view_mapping_flags,
            original_mapping.is_file_based() ? mapping_object_type::file : mapping_object_type::memory
        ).data()
    };
#   else // macOS
    auto new_address
    {
        posix::mmap
        (
            tail_target_address,
            additional_tail_size,
            original_mapping.view_mapping_flags.protection,
            original_mapping.view_mapping_flags.flags,
            original_mapping.get(),
            kernel_current_size
        )
    };
    if ( ( new_address != tail_target_address ) && ( current_address != nullptr ) )
    {
        BOOST_VERIFY( ::munmap( new_address, additional_tail_size ) == 0 );
        new_address = nullptr;
    }
#   endif

    if ( new_address ) [[ likely ]]
    {
        if ( current_address != nullptr ) [[ likely ]]
        {
            BOOST_ASSUME( new_address == current_address + kernel_current_size );
            static_cast<span &>( *this ) = { current_address, target_size };
        }
        else
        {
            static_cast<span &>( *this ) = { static_cast<std::byte *>( new_address ), target_size };
        }
        return err::success;
    }

    // Step 2: fallback — create a new full-size mapping and transfer data.

#   ifdef _WIN32
    // Step 2a: overreserve — new section view with 2x trailing placeholder.
    // unmap_concatenated() frees trailing placeholders, so the lifecycle is clean.
    if ( auto * const base{ detail::overreserve_section_map(
        original_mapping.get(),
        target_size,
        0, // section_offset (full remap from beginning)
        original_mapping.view_mapping_flags.page_protection
    ) } )
    {
        if ( current_address )
        {
            if ( original_mapping.view_mapping_flags.is_cow() )
                std::memcpy( base, current_address, current_size );
            else
                BOOST_ASSERT_MSG( std::memcmp( base, current_address, current_size ) == 0, "overreserve garbled data" );
        }
        this->unmap();
        static_cast<span &>( *this ) = { base, target_size };
        return err::success;
    }
#   endif

    // Step 2b: plain fallback — no over-reservation.
    auto remapped_span{ this->map( original_mapping, 0, target_size )() };
    if ( !remapped_span )
        return remapped_span.error();

#   ifdef _WIN32
    if ( original_mapping.view_mapping_flags.is_cow() )
    {
        // COW (PAGE_WRITECOPY) view: the new view sees original section data,
        // not our private (COW-modified) pages.  Must copy from old view.
        // TODO: use dirty_tracker to copy only modified pages.
        std::memcpy( const_cast<std::byte *>( remapped_span->data() ), this->data(), this->size() );
    }
    else
    {
        // Non-COW: new view maps the same section data.
        BOOST_ASSERT_MSG( std::memcmp( remapped_span->data(), this->data(), this->size() ) == 0, "View expansion garbled data." );
    }
#   else // macOS
    if ( !original_mapping.is_file_based() )
    {
        // mach_vm_remap: zero-copy page transfer from old to new mapping.
        auto new_addr{ reinterpret_cast<mach_vm_address_t>( const_cast<std::byte *>( remapped_span->data() ) ) };
        auto const kr
        {
            mach::vm_remap_overwrite
            (
                &new_addr,
                current_size,
                current_address,
                FALSE,
                VM_INHERIT_NONE
            )
        };
        if ( kr != KERN_SUCCESS ) [[ unlikely ]]
        {
            std::memcpy( const_cast<std::byte *>( remapped_span->data() ), this->data(), this->size() );
        }
    }
    else
    {
        BOOST_ASSERT_MSG( std::memcmp( remapped_span->data(), this->data(), this->size() ) == 0, "View expansion garbled data." );
    }
#   endif
    *this = std::move( *remapped_span );
    return err::success;
#endif // platform
}

template class basic_mapped_view<false>;
template class basic_mapped_view<true >;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
