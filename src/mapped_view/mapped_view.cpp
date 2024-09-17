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
    auto const current_address    { const_cast</*mrmlj*/std::byte *>( this->data() ) };
    auto const current_size       {                                   this->size()   };
    auto const kernel_current_size{ align_up( current_size, commit_granularity )     };
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
#endif // linux mremap
    auto const current_offset{ 0U }; // TODO: what if this is an offset view?
    auto const target_offset { current_offset + kernel_current_size };
    auto const additional_tail_size{ target_size - kernel_current_size };
    auto const tail_target_address { current_address + kernel_current_size };
#ifndef _WIN32
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
        ( current_address != nullptr             )    // in case of starting with an empty/null/zero-sized views (is it worth the special handling?)
    )
    {
        BOOST_VERIFY( ::munmap( new_address, additional_tail_size ) == 0 );
        new_address = nullptr;
    }
#else
    ULARGE_INTEGER const win32_offset{ .QuadPart = target_offset };
    auto const new_address
    { // TODO this (appending) requires complexity to be added on the unmap side (see mapper::unmap)
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
#endif
    if ( new_address ) [[ likely ]]
    {
        if ( current_address != nullptr ) [[ likely ]]
        {
            BOOST_ASSUME( new_address == current_address + kernel_current_size );
#       ifdef __linux__
            BOOST_ASSERT_MSG( false, "mremap failed but an overlapping mmap succeeded!?" ); // behaviour investigation
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
    *this = std::move( *remapped_span );
    return err::success;
} // view::expand()

template class basic_mapped_view<false>;
template class basic_mapped_view<true >;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
