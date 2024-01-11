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
#include <psi/vm/mapped_view/mapped_view.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

template <bool read_only>
BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
err::fallible_result<basic_mapped_view<read_only>, error> BOOST_CC_REG
basic_mapped_view<read_only>::map
(
    mapping        &       source_mapping,
    flags::viewing   const flags,
    std::uint64_t    const offset,
    std::size_t      const desired_size
) noexcept
{
    auto const mapped_span{ mapper::map( source_mapping, flags, offset, desired_size ) };
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
    mapper::unmap( reinterpret_cast<mapped_span const &>( static_cast<span const &>( *this ) ) );
}


template <bool read_only>
void basic_mapped_view<read_only>::flush() const noexcept requires( !read_only ) { mapper::flush( *this ); }


template <bool read_only>
void basic_mapped_view<read_only>::shrink( std::size_t const target_size ) noexcept
{
    mapper::shrink( reinterpret_cast<mapped_span const &>( static_cast<span const &>( *this ) ), target_size );
    static_cast<span &>( *this ) = { this->data(), target_size };
}


template <bool read_only>
fallible_result<void>
basic_mapped_view<read_only>::expand( std::size_t const target_size, mapping & original_mapping ) noexcept
{
    auto const current_address{ const_cast</*mrmlj*/std::byte *>( this->data() ) };
    auto const current_size   {                                   this->size()   };
#if defined( __linux__ )
    if ( auto const new_address{ ::mremap( current_address, current_size, target_size, std::to_underlying( reallocation_type::moveable ) ) }; new_address != MAP_FAILED ) [[ likely ]]
    {
        static_cast<span &>( *this ) = { static_cast<typename span::pointer>( new_address ), target_size };
        return err::success;
    }
#endif // linux mremap
#ifndef _WIN32
    auto const new_address
    {
        posix::mmap
        (
            current_address,
            target_size,
            original_mapping.view_mapping_flags.protection,
            original_mapping.view_mapping_flags.flags | MAP_FIXED,
            original_mapping.get(),
            0 /*TODO: what if this is an offset view?*/
        )
    };
    if ( new_address ) [[ likely ]]
    {
        BOOST_ASSUME( new_address == current_address );
#   ifdef __linux__
        BOOST_ASSERT_MSG( false, "mremap failed but an overlapping mmap succeeded!?" ); // behaviour investigation
#   endif
        static_cast<span &>( *this ) = { static_cast<typename span::pointer>( new_address ), target_size };
        return err::success;
    }
#else
    auto const current_offset{ 0U }; // TODO: what if this is an offset view?
    auto const additional_end_size{ target_size - current_size };
    ULARGE_INTEGER const win32_offset{ .QuadPart = current_offset + current_size };
    auto const new_address
    {
        ::MapViewOfFileEx
        (
            original_mapping.get(),
            original_mapping.view_mapping_flags.map_view_flags,
            win32_offset.HighPart,
            win32_offset.LowPart,
            additional_end_size,
            current_address + current_size
        )
    };
    if ( new_address ) [[ likely ]]
    {
        BOOST_ASSUME( new_address == current_address + current_size );
        static_cast<span &>( *this ) = { current_address, target_size };
        return err::success;
    }
#endif
    // paying with peak VM space usage and/or fragmentation for the strong guarantee
    auto remapped_span{ this->map( original_mapping, 0, target_size )() };
    if ( !remapped_span )
        return remapped_span.error();
    *this = std::move( *remapped_span );
    return err::success;
} // view::expand()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
