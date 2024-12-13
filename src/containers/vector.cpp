////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric.
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
#include <psi/vm/containers/vector.hpp>

#include <psi/vm/mapped_view/ops.hpp>

#include <stdexcept>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace detail
{
    [[ noreturn, gnu::cold ]] void throw_out_of_range() { throw std::out_of_range( "vm::vector access out of bounds" ); }
    [[ noreturn, gnu::cold ]] void throw_bad_alloc   () { throw std::bad_alloc(); }
} // namespace detail

void contiguous_container_storage_base::close() noexcept
{
    mapping_.close();
    unmap();
}

void contiguous_container_storage_base::flush_async   ( std::size_t const beginning, std::size_t const size ) const noexcept { vm::flush_async   ( mapped_span({ view_.subspan( beginning, size ) }) ); }
void contiguous_container_storage_base::flush_blocking( std::size_t const beginning, std::size_t const size ) const noexcept { vm::flush_blocking( mapped_span({ view_.subspan( beginning, size ) }), mapping_.underlying_file() ); }

void * contiguous_container_storage_base::grow_to( std::size_t const target_size )
{
    BOOST_ASSUME( target_size > mapped_size() );
    // basic (1.5x) geometric growth implementation
    // TODO: make this configurable (and probably move out/down to container
    // class templates)
    auto const current_capacity{ storage_size() };
    if ( current_capacity < target_size ) [[ unlikely ]]
    {
        auto const new_capacity{ std::max( target_size, current_capacity * 3U / 2U ) };
        set_size( mapping_, new_capacity );
    }
    return expand_view( target_size );
}

void * contiguous_container_storage_base::expand_view( std::size_t const target_size )
{
    BOOST_ASSERT( get_size( mapping_ ) >= target_size );
    view_.expand( target_size, mapping_ );
    return data();
}

void * contiguous_container_storage_base::shrink_to( std::size_t const target_size ) noexcept( mapping::views_downsizeable )
{
    if constexpr ( mapping::views_downsizeable )
    {
        view_.shrink( target_size );
        set_size( mapping_, target_size )().assume_succeeded();
    }
    else
    {
        view_.unmap();
        set_size( mapping_, target_size )().assume_succeeded();
        view_ = mapped_view::map( mapping_, 0, target_size );
    }
    return data();
}

void contiguous_container_storage_base::shrink_mapped_size_to( std::size_t const target_size ) noexcept( mapping::views_downsizeable )
{
    if constexpr ( mapping::views_downsizeable )
    {
        view_.shrink( target_size );
    }
    else
    {
        view_.unmap();
        view_ = mapped_view::map( mapping_, 0, target_size );
    }
}

void contiguous_container_storage_base::free() noexcept
{
    view_.unmap();
    set_size( mapping_, 0 )().assume_succeeded();
}

void contiguous_container_storage_base::shrink_to_fit() noexcept
{
    set_size( mapping_, mapped_size() )().assume_succeeded();
}

void * contiguous_container_storage_base::resize( std::size_t const target_size )
{
    if ( target_size > mapped_size() ) return grow_to( target_size );
    else                               return shrink_to( target_size );
}

void contiguous_container_storage_base::reserve( std::size_t const new_capacity )
{
    if ( new_capacity > storage_size() )
        set_size( mapping_, new_capacity );
}

err::fallible_result<std::size_t, error>
contiguous_container_storage_base::map_file( file_handle && file, std::size_t const header_size ) noexcept
{
    if ( !file )
        return error{};
    auto const file_size{ get_size( file ) };
    BOOST_ASSERT_MSG( file_size <= std::numeric_limits<std::size_t>::max(), "Pagging file larger than address space!?" );
    auto const existing_size{ static_cast<std::size_t>( file_size ) };
    bool const created_file { existing_size == 0 };
    auto const mapping_size { std::max<std::size_t>( header_size, existing_size ) };
    BOOST_ASSERT_MSG( existing_size >= header_size || created_file, "Corrupted file: bogus on-disk size" );
    if ( created_file && !mapping::create_mapping_can_set_source_size )
        set_size( file, mapping_size );

    return map( std::move( file ), mapping_size );
}

err::fallible_result<std::size_t, error>
contiguous_container_storage_base::map( file_handle && file, std::size_t const mapping_size ) noexcept
{
    using ap    = flags::access_privileges;
    using flags = flags::mapping;
    mapping_ = create_mapping
    (
        std::move( file ),
        ap::object{ ap::readwrite },
        ap::child_process::does_not_inherit,
#   ifdef __linux__
        // TODO solve in a cleaner/'in a single place' way
        // https://bugzilla.kernel.org/show_bug.cgi?id=8691 mremap: Wrong behaviour expanding a MAP_SHARED anonymous mapping
        !file ? flags::share_mode::hidden :
#   endif
        flags::share_mode::shared,
        mapping::supports_zero_sized_mappings
            ? mapping_size
            : std::max( std::size_t{ 1 }, mapping_size )
    );
    if ( !mapping_ )
        return error{};

    if ( mapping_size ) [[ likely ]]
    {
        auto view{ mapped_view::map( mapping_, 0, mapping_size ).as_result_or_error() };
        if ( !view )
            return view.error();
        view_ = *std::move( view );
        BOOST_ASSERT( view_.size() == mapping_size );
    }
    else
    {
        view_.unmap();
    }

    return std::size_t{ mapping_size };
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
