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
#include <psi/vm/align.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>

#include <cstring> // memcpy
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
    std   ::size_t   desired_size
) noexcept;

BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L1 )
void unmap( mapped_span view ) noexcept;

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
    auto const mapped_span{ vm::map( source_mapping, flags, offset, desired_size ) };
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
        unmap_partial
        ({
            align_up  ( view.data() + target_size, commit_granularity ),
            align_down( view.size() - target_size, commit_granularity )
        });
    }
} // anonymous namepsace

template <bool read_only>
void basic_mapped_view<read_only>::shrink( std::size_t const target_size ) noexcept
{
    do_shrink( reinterpret_cast<mapped_span const &>( static_cast<span const &>( *this ) ), target_size );
    static_cast<span &>( *this ) = { this->data(), target_size };
}


template <bool read_only>
fallible_result<void>
basic_mapped_view<read_only>::expand( std::size_t const target_size, mapping & original_mapping ) noexcept
{
    // TODO kill duplication with remap.cpp::expand()

    auto const current_address    { const_cast<std::byte *>( this->data() )      };
    auto const current_size       {                          this->size()        };
    auto const kernel_current_size{ align_up( current_size, commit_granularity ) };
    if ( kernel_current_size >= target_size )
    {
        BOOST_ASSUME( current_address );
        BOOST_ASSUME( current_size    );
        static_cast<span &>( *this ) = { current_address, target_size };
        return err::success;
    }

#if defined( __linux__ )
    if
    (
        auto const new_address{ ::mremap( current_address, current_size, target_size, std::to_underlying( reallocation_type::moveable ) ) };
        new_address != MAP_FAILED
    ) [[ likely ]]
    {
        static_cast<span &>( *this ) = { static_cast<typename span::pointer>( new_address ), target_size };
        return err::success;
    }
    return error_t{};
#else
    auto const current_offset{ 0U }; // TODO: what if this is an offset view?
    auto const target_offset { current_offset + kernel_current_size };
    auto const additional_tail_size{ target_size - kernel_current_size };
    auto const tail_target_address { current_address + kernel_current_size };
#ifdef _WIN32
    ULARGE_INTEGER const win32_offset{ .QuadPart = target_offset };
    auto const new_address
    {
        ::MapViewOfFileEx
        (
            original_mapping.get(),
            original_mapping.view_mapping_flags.map_view_flags,
            win32_offset.HighPart,
            win32_offset.LowPart,
            additional_tail_size,
            tail_target_address
        )
    };
#else
    auto new_address
    {
        posix::mmap
        (
            tail_target_address,
            additional_tail_size,
            original_mapping.view_mapping_flags.protection,
            original_mapping.view_mapping_flags.flags,
            original_mapping.get(),
            target_offset
        )
    };
    if
    (
        ( new_address     != tail_target_address ) && // On POSIX the target address is only a hint (while MAP_FIXED may overwrite existing mappings)
        ( current_address != nullptr             )    // in case of starting with an empty/null/zero-sized view (is it worth the special handling?)
    )
    {
        BOOST_VERIFY( ::munmap( new_address, additional_tail_size ) == 0 );
        new_address = nullptr;
    }
#endif
    if ( new_address ) [[ likely ]]
    {
        if ( current_address != nullptr ) [[ likely ]]
        {
            BOOST_ASSUME( new_address == current_address + kernel_current_size );
#       ifdef __linux__ // no longer exercised path (i.e. linux only relies on mremap)
            BOOST_ASSERT_MSG( false, "mremap failed but an adjacent mmap succeeded!?" ); // behaviour investigation
#       endif
            static_cast<span &>( *this ) = {                          current_address, target_size };
        }
        else
        {
            static_cast<span &>( *this ) = { static_cast<std::byte *>( new_address ) , target_size };
        }
        return err::success;
    }
    // paying with peak VM space usage and/or fragmentation for the strong guarantee
    auto remapped_span{ this->map( original_mapping, 0, target_size )() };
    if ( !remapped_span )
        return remapped_span.error();
#ifndef _WIN32
    // Linux handles relocation-on-expansion implicitly with mremap (it even
    // supports creating new mappings of the same pages/physical memory by
    // specifying zero for old_size -> TODO use this to implement 'multimappable'
    // mappings, 'equivalents' of NtCreateSection instead of going through shm),
    // Windows tracks the source data/backing storage through the mapping
    // handle. For others we have to the the new-copy-free old dance - but this
    // is a quick-fix as this still does not support 'multiply remappable
    // mapping' semantics a la NtCreateSection - TODO: shm_mkstemp, SHM_ANON,
    // memfd_create...
    // https://github.com/lassik/shm_open_anon
    if ( !original_mapping.is_file_based() )
        std::memcpy( const_cast<std::byte *>( remapped_span->data() ), this->data(), this->size() );
    else
#endif
        BOOST_ASSERT_MSG( std::memcmp( remapped_span->data(), this->data(), this->size() ) == 0, "View expansion garbled data." );
    *this = std::move( *remapped_span );
    return err::success;
#endif // end of 'no linux mremap' implementation
} // view::expand()

template class basic_mapped_view<false>;
template class basic_mapped_view<true >;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
