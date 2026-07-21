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

#include <psi/vm/align.hpp>
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
    [[ noreturn, gnu::cold ]] void throw_length_error()
    {
        throw std::length_error{ "psi::vm: requested size exceeds the allocator's addressable byte range" };
    }
#endif
} // namespace detail

void mem_mapping::publish_size() noexcept
{
    if ( !has_attached_storage() )
        return;

    // Store only on an actual change - publishing an unchanged length would
    // dirty the header page for nothing.
    auto & persisted{ persisted_size() };
    if ( persisted != live_size_ )
        persisted = live_size_;
}

void mem_mapping::close() noexcept
{
    // A clean detach is a commit point: publish the live length so an orderly
    // shutdown persists exactly what the container spans (this is what keeps
    // the change invisible to users who never crash). An abnormal termination
    // by definition does not reach here, which is precisely why the persisted
    // length then still denotes the last committed extent.
    publish_size();
    unmap();
    mapping_.close();
    live_size_ = 0;
}

// A flush that starts at 0 covers sizes_hdr, so it is also the point at which
// the live length becomes the committed one. Doing it here (rather than on
// every growth) is the whole point of the split: after an abnormal
// termination the persisted length denotes the last committed extent, and
// everything physically beyond it is uncommitted by construction. Data-only
// flushes (beginning != 0) must NOT publish - a length made durable ahead of
// the bytes it spans would be exactly the corruption this prevents.
void                              mem_mapping::flush_async   ( std::size_t const beginning, std::size_t const size )       noexcept { if ( beginning == 0 ) { publish_size(); }        vm::flush_async   ( mapped_span({ view_.subspan( beginning, size ) }) ); }
err::fallible_result<void, error> mem_mapping::flush_blocking( std::size_t const beginning, std::size_t const size )       noexcept { if ( beginning == 0 ) { publish_size(); } return vm::flush_blocking( mapped_span({ view_.subspan( beginning, size ) }), mapping_.underlying_file() ); }
[[ gnu::pure ]]
mem_mapping::size_type
mem_mapping::client_to_storage_size( size_type const sz ) const noexcept
{
    return sz + get_sizes().total_hdr_size();
}

namespace
{
    /// The on-disk length to give a file that has to hold storage_size bytes.
    ///
    /// Resizing a file is documented as a metadata-only operation, and it is -
    /// as far as block allocation goes - but on a filesystem which journals the
    /// size change (e.g. xfs) an *extending* resize of a file that both has
    /// dirty mmap pages and an unaligned EOF must first flush and wait for the
    /// tail block (xfs_setattr_size -> filemap_write_and_wait_range ->
    /// folio_wait_writeback) before it can move the size across it. That wait is
    /// a synchronous device round trip; on network-attached storage it dominates
    /// everything else the resize does. Neither condition is sufficient alone:
    /// measured per-call cost on xfs over a network block device is ~0.7us for a
    /// clean file, ~1.1us for dirty-but-aligned and ~645us for dirty+unaligned.
    ///
    /// Rounding the length up to a page keeps the EOF off the dirty tail block
    /// and so avoids the wait entirely. The cost is under one page of slack per
    /// file: the *logical* size lives in the header (sizes_hdr::data_size) and
    /// the container's capacity comes from the mapped view (vm_capacity), so the
    /// surplus is nothing but spare fs_capacity - it is not visible as size and
    /// needs no format change.
    ///
    /// commit_granularity (the page size) rather than the filesystem's
    /// st_blksize: it is the granularity the mmap write-back path itself works
    /// in, it is what makes the EOF fall on a page boundary, and it avoids a
    /// per-file statvfs. Filesystems whose block size exceeds the page size
    /// simply see a partial win rather than a wrong result.
    [[ gnu::const ]] std::size_t file_length_for( std::size_t const storage_size ) noexcept
    {
        return align_up( storage_size, commit_granularity );
    }
} // anonymous namespace

[[ gnu::noinline ]]
void * mem_mapping::expand_capacity( std::size_t const target_capacity )
{
    BOOST_ASSUME( target_capacity > mapped_size() );
    // Exact-size expansion only. Geometric growth is the vector's responsibility.
    auto const current_fc_capacity{ storage_size() };
    if ( current_fc_capacity < target_capacity ) [[ unlikely ]]
        set_size( mapping_, file_length_for( target_capacity ) );
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
    auto const current_file_length{ storage_size() };
    auto const storage_size       { client_to_storage_size( target_size ) };
    // Keep the on-disk EOF page-aligned here too - a file that shrank to an
    // unaligned length would pay the tail-block flush on its next extension
    // (see file_length_for). A shrink never *grows* the file: when the aligned
    // length would not actually be smaller the resize is skipped outright,
    // which also spares the syscall for the common small-shrink case.
    auto const new_file_length{ file_length_for( storage_size ) };
    auto const resize_file    { new_file_length < current_file_length };
    if constexpr ( mapping::views_downsizeable )
    {
        view_.shrink( storage_size );
        if ( resize_file )
            set_size( mapping_, new_file_length )().assume_succeeded();
    }
    else
    {
        auto const do_unmap{ view_.size() != storage_size };
        if ( do_unmap )
            view_.unmap();
        if ( resize_file )
            set_size( mapping_, new_file_length )().assume_succeeded();
        if ( do_unmap )
            view_ = extendable_mapped_view::map( mapping_, 0, storage_size );
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
        view_ = extendable_mapped_view::map( mapping_, 0, target_size );
    }
}


void mem_mapping::shrink_to_fit() noexcept
{
    shrink_to_slow( live_size() );
}

void mem_mapping::reserve( size_type const new_capacity )
{
    if ( new_capacity > vm_capacity() ) [[ unlikely ]]
        expand_capacity( client_to_storage_size( new_capacity ) );
}

void * mem_mapping::shrink_to( size_type const target_size ) noexcept
{
    auto & sz{ live_size() };
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
    BOOST_ASSUME( live_size() == target_size );
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
        // else: validated below - either way the live length starts at the
        // persisted (committed) one, which for a fresh file is 0.
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
                // Corrupted file: bogus or unexpected on-disk header.
                // Detach WITHOUT publishing: close() would write live_size_
                // into the very header just declared corrupt, quietly
                // repairing it and hiding the corruption from the next open
                // (and from any consistency checker running over the file).
                // Nothing here has been validated, so there is no length
                // worth persisting - only resources worth releasing.
                unmap();
                mapping_.close();
                return error{ error::invalid_data };
            }
        }
        // Seed the live length from the committed one: an attach observes
        // exactly what the last header-covering flush (or clean detach)
        // published - which, after an abnormal termination, is the last
        // committed extent rather than an in-flight cursor.
        live_size_ = get_sizes().data_size;
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
    live_size_  = data_size; // anonymous storage: nothing to commit to, live == committed
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
            live_size_  = data_size; // ephemeral storage: live == committed
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
        auto view{ extendable_mapped_view::map( mapping_, 0, mapping_size ).as_result_or_error() };
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
