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
#include <psi/vm/containers/vm_vector.hpp>

#include <psi/vm/mapped_view/ops.hpp>

#include <boost/assert.hpp>

#ifdef __linux__
#   include <sys/mman.h>
#   if __has_include( <sys/memfd.h> )
#       include <sys/memfd.h>
#   endif
#   ifndef MFD_CLOEXEC
#       define MFD_CLOEXEC 0x0001U
        extern "C" int memfd_create( char const *, unsigned int ) noexcept;
#   endif
#   include <unistd.h> // ftruncate, close
#endif

#include <stdexcept>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace detail
{
    [[ noreturn, gnu::cold ]] void throw_out_of_range( char const * const msg ) { throw std::out_of_range( msg ); }
#if PSI_MALLOC_OVERCOMMIT != PSI_OVERCOMMIT_Full
    [[ noreturn, gnu::cold ]] void throw_bad_alloc()
    {
#   ifdef _MSC_VER
        std::_Xbad_alloc();
#   else
        throw std::bad_alloc{};
#   endif
    }
#endif
} // namespace detail

void mem_mapping::close() noexcept
{
    unmap();
    mapping_.close();
}

void mem_mapping::flush_async   ( std::size_t const beginning, std::size_t const size ) const noexcept { vm::flush_async   ( mapped_span({ view_.subspan( beginning, size ) }) ); }
void mem_mapping::flush_blocking( std::size_t const beginning, std::size_t const size ) const noexcept { vm::flush_blocking( mapped_span({ view_.subspan( beginning, size ) }), mapping_.underlying_file() ); }
[[ gnu::pure ]]
mem_mapping::size_type
mem_mapping::client_to_storage_size( size_type const sz ) const noexcept
{
    return sz + get_sizes().total_hdr_size();
}

[[ gnu::noinline ]]
void * mem_mapping::expand_capacity( std::size_t const target_capacity )
{
    BOOST_ASSUME( target_capacity > mapped_size() );
    // Exact-size expansion only. Geometric growth is the vector's responsibility.
    auto const current_fc_capacity{ storage_size() };
    if ( current_fc_capacity < target_capacity ) [[ unlikely ]]
        set_size( mapping_, target_capacity );
    return expand_view( target_capacity );
}

void * mem_mapping::expand_view( std::size_t const target_size )
{
    BOOST_ASSERT( get_size( mapping_ ) >= target_size );
    view_.expand( target_size, mapping_ );
    return data();
}

[[ gnu::noinline ]]
void * mem_mapping::shrink_to_slow( std::size_t const target_size ) noexcept( mapping::views_downsizeable )
{
    auto const storage_size{ client_to_storage_size( target_size ) };
    if constexpr ( mapping::views_downsizeable )
    {
        view_.shrink( storage_size );
        set_size( mapping_, storage_size )().assume_succeeded();
    }
    else
    {
        auto const do_unmap{ view_.size() != storage_size };
        if ( do_unmap )
            view_.unmap();
        set_size( mapping_, storage_size )().assume_succeeded();
        if ( do_unmap )
            view_ = mapped_view::map( mapping_, 0, storage_size );
    }
    return data();
}

void mem_mapping::shrink_mapped_size_to( std::size_t const target_size ) noexcept( mapping::views_downsizeable )
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


void mem_mapping::shrink_to_fit() noexcept
{
    shrink_to_slow( stored_size() );
}

void mem_mapping::reserve( size_type const new_capacity )
{
    if ( new_capacity > vm_capacity() ) [[ unlikely ]]
        expand_capacity( client_to_storage_size( new_capacity ) );
}

void * mem_mapping::shrink_to( size_type const target_size ) noexcept
{
    auto & sz{ stored_size() };
    if ( sz == target_size ) { // minimize every bit of unnecessary page touching/dirtying
        return data();
    }

    if ( align_down( sz, commit_granularity ) == align_down( target_size, commit_granularity ) ) {
        sz = target_size;
        return data();
    }

    sz = target_size;
    return shrink_to_slow( target_size );
}

void mem_mapping::resize( size_type const target_size )
{
    if ( target_size > size() ) {
        grow_to( target_size );
    } else {
        // or skip this like std::vector and rely on an explicit shrink_to_fit() call?
        shrink_to( target_size );
    }
    BOOST_ASSUME( stored_size() == target_size );
}

[[ gnu::pure, nodiscard ]]
std::span<std::byte> mem_mapping::header_storage() noexcept
{
    auto const & sizes{ get_sizes() };
    return
    {
        std::assume_aligned<header_info::minimal_subheader_alignment>( mapped_data() + sizes.hdr_offset ),
        sizes.client_hdr_size()
    };
}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
[[ gnu::const ]] constexpr
mem_mapping::sizes_hdr
mem_mapping::unpack( header_info const hdr_info ) noexcept
{
    auto const         base_hdr_size{ align_up( std::uint8_t{ sizeof( sizes_hdr ) }, hdr_info.final_alignment() ) };
    auto const       client_hdr_size{ hdr_info.final_header_size() };
    auto const        total_hdr_size{ align_up( base_hdr_size + client_hdr_size, hdr_info.data_extra_alignment ) };
    auto const final_client_hdr_size{ total_hdr_size - base_hdr_size };
    return
    {
        .data_offset = total_hdr_size,
        .hdr_size    = static_cast<std::uint32_t>( final_client_hdr_size ),
        .hdr_offset  = static_cast<std::uint32_t>( base_hdr_size ),
        .data_size   = 0
    };
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

[[ gnu::cold ]]
err::result_or_error<void, error>
mem_mapping::map_file( file_handle file, flags::named_object_construction_policy const policy, header_info const hdr_info ) noexcept
{
    if ( !file )
        return error{};
    BOOST_ASSERT_MSG( get_size( file ) <= std::numeric_limits<std::size_t>::max(), "Pagging file larger than address space!?" );
    using construction = flags::named_object_construction_policy;
    std::size_t existing_size;
    bool        created_file;
    switch ( policy )
    {
        case construction::create_new                     : created_file = true ; existing_size = 0; break;
        case construction::create_new_or_truncate_existing: created_file = true ; existing_size = 0; break;
        case construction::open_and_truncate_existing     : created_file = true ; existing_size = 0; break;
        case construction::open_existing                  : created_file = false; existing_size = static_cast<std::size_t>( get_size( file ) ); break;
        case construction::open_or_create                 : existing_size = static_cast<std::size_t>( get_size( file ) ); created_file = ( existing_size != 0 ); break;
    }
    BOOST_ASSERT( existing_size == static_cast<std::size_t>( get_size( file ) ) );
    auto const hdr{ unpack( hdr_info ) };
    auto const total_hdr_size{ hdr.total_hdr_size() };
    auto mapping_size{ existing_size };
    if ( created_file )
    {
        BOOST_ASSUME( existing_size == 0 );
        mapping_size = total_hdr_size;
        if constexpr ( !mapping::create_mapping_can_set_source_size )
        {
            auto sz{ set_size( file, mapping_size )() };
            if ( !sz ) [[ unlikely ]]
                return sz.error();
        }
    }
    else
    {
        if ( existing_size < total_hdr_size ) [[ unlikely ]]
        {
            // Corrupted file: bogus or unexpected on-disk size
            return error{ error::invalid_data };
        }
    }

    auto map_rslt{ map( std::move( file ), mapping_size ) };
    if ( map_rslt )
    {
        auto & on_disk_sizes{ get_sizes() };
        if ( created_file )
        {
            BOOST_ASSUME( hdr          .data_size == 0 );
            BOOST_ASSUME( on_disk_sizes.data_size == 0 );
            on_disk_sizes = hdr;
        }
        else
        {
            auto match{ on_disk_sizes };
            if ( hdr_info.extendable )
            {
#           ifdef __GNUC__
#           pragma GCC diagnostic push
#           pragma GCC diagnostic ignored "-Wconversion"
#           endif
                match.data_offset = std::min( match.data_offset, hdr.data_offset );
                match.hdr_size    = std::min( match.data_offset, hdr.hdr_size    );
                match.hdr_offset  = std::min( match.data_offset, hdr.hdr_offset  );
#           ifdef __GNUC__
#           pragma GCC diagnostic pop
#           endif
            }
            if
            (
                ( match.data_offset       != hdr.data_offset       ) ||
                ( match.client_hdr_size() != hdr.client_hdr_size() ) ||
                ( match.data_size          > mapping_size          )
            ) [[ unlikely ]]
            {
                // Corrupted file: bogus or unexpected on-disk header
                close();
                return error{ error::invalid_data };
            }
        }
    }
    return map_rslt.propagate();
}
[[ gnu::cold ]]
err::result_or_error<void, error> mem_mapping::map_memory( size_type const data_size, header_info const hdr_info ) noexcept
{
    auto hdr{ unpack( hdr_info ) };
    auto map_success{ map( {}, hdr.total_hdr_size() + data_size ) };
    if ( !map_success )
        return map_success.error();
    hdr.data_size = data_size;
    get_sizes() = hdr;
    return err::success;
}

[[ gnu::cold ]]
err::result_or_error<void, error> mem_mapping::map_cow_memory( size_type const data_size, header_info const hdr_info ) noexcept
{
#ifdef __linux__
    // On Linux, create a memfd-backed mapping so that future COW copies (via
    // the copy constructor) are zero-copy: dup(fd) + MAP_PRIVATE, instead of
    // requiring an initial memcpy into a memfd. The memfd is anonymous but
    // fd-backed, so it participates in the regular file-backed COW path.
    auto const total_size{ unpack( hdr_info ).total_hdr_size() + data_size };
    auto const mfd{ ::memfd_create( "psi_vm_cow_src", MFD_CLOEXEC ) };
    if ( mfd != -1 )
    {
        if ( ::ftruncate( mfd, static_cast<off_t>( total_size ) ) == 0 )
        {
            auto hdr{ unpack( hdr_info ) };
            auto map_success{ map( file_handle{ mfd }, total_size ) };
            if ( !map_success )
                return map_success.error();
            mapping_.set_ephemeral(); // memfd: fd-backed but not on-disk
            hdr.data_size = data_size;
            get_sizes() = hdr;
            return err::success;
        }
        ::close( mfd );
    }
    // memfd_create or ftruncate failed -- fall back to regular anonymous
#endif // __linux__
    return map_memory( data_size, hdr_info );
}

err::result_or_error<void, error>
mem_mapping::map( file_handle file, std::size_t const mapping_size ) noexcept
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
            : std::max<std::size_t>( 1, mapping_size )
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
        unmap();
    }

    return err::success;
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
