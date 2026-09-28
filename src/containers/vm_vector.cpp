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
#include <psi/vm/allocators/allocator_base.hpp> // detail::throw_bad_alloc
#include <psi/vm/mapped_view/ops.hpp>

#include <psi/build/attributes.hpp>

#include <boost/assert.hpp>

#ifdef __linux__
#   include "../allocation/expand_linux.hpp" // pmd_span
#   include <sys/mman.h>
#   if __has_include( <sys/memfd.h> )
#       include <sys/memfd.h>
#   endif
#   ifndef MFD_CLOEXEC
#       define MFD_CLOEXEC 0x0001U
        extern "C" int memfd_create( char const *, unsigned int ) noexcept;
#   endif
#   include <unistd.h> // ftruncate, close
#   ifndef MADV_COLLAPSE // Linux 6.1
#       define MADV_COLLAPSE 25
#   endif
#elif defined( __APPLE__ )
#   include <sys/mman.h>
#elif defined( _WIN32 )
#   include <psi/vm/detail/nt.hpp>
#endif

#include <algorithm> // min
#include <cstring> // memcpy
#include <stdexcept>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace detail
{
    [[ noreturn ]] PSI_COLD void throw_out_of_range( char const * const msg ) { throw std::out_of_range( msg ); }
#if PSI_MALLOC_OVERCOMMIT != PSI_OVERCOMMIT_Full
    [[ noreturn ]] PSI_COLD void throw_bad_alloc()
    {
#   ifdef _MSC_VER
        std::_Xbad_alloc();
#   else
        throw std::bad_alloc{};
#   endif
    }
#endif
    // Deliberately outside the overcommit split above: a length error reports a
    // request the size type cannot express, which no allocation policy makes
    // acceptable. See the declaration in allocators/allocator_base.hpp.
    [[ noreturn ]] PSI_COLD void throw_length_error()
    {
        throw std::length_error{ "psi::vm: requested size exceeds the allocator's addressable byte range" };
    }
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
    live_size_     = 0;
    object_extent_ = 0;
#if PSI_VM_HUGE_PAGE_MAX_COVERAGE && defined( __linux__ ) && !defined( __ANDROID__ )
    huge_pages_    = false;
#endif
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

bool mem_mapping::views_privately() const noexcept
{
#ifdef _WIN32
    return mapping_.view_mapping_flags.is_cow();
#else
    // An anonymous MAP_PRIVATE mapping is private too, but it views no object
    return mapping_.has_fd() && mapping_.view_mapping_flags.is_cow();
#endif
}

// A private (copy-on-write) view - a COW clone - shares the object it views
// (a file, a memfd, a pagefile section) with the container it was cloned
// from, which alone owns its length: resizing it from here would resize the
// source's object under it (and, shrinking, cut off pages the source still
// maps), and mapping more of it would show the source's bytes as the
// clone's. So a clone grows past the object with anonymous memory of its own
// (grow_privately()). Only where that cannot be done does it move off the
// object entirely, into memory of its own, carrying the header and its live
// elements (the rest reads as zero, as fresh growth anywhere does): from
// there on it is an ordinary memory backed container - it grows, and clones,
// like one.
PSI_COLD
void mem_mapping::move_into_memory( std::size_t const mapped_size )
{
    auto const carried_size{ get_sizes().data_offset + live_size_ };
    BOOST_ASSUME( carried_size <= mapped_size );
    auto       file_view   { std::move( view_    ) };
    auto       file_mapping{ std::move( mapping_ ) };
    if ( auto const mapped{ map( {}, mapped_size ) }; !mapped ) [[ unlikely ]]
    {
        view_    = std::move( file_view    );
        mapping_ = std::move( file_mapping );
        detail::throw_bad_alloc();
    }
    std::memcpy( view_.data(), file_view.data(), carried_size );
    object_extent_ = 0;
}

[[ gnu::noinline ]]
void * mem_mapping::expand_capacity( std::size_t target_capacity )
{
    BOOST_ASSUME( target_capacity > mapped_size() );
    auto const private_view{ views_privately() };
#if defined( __linux__ ) && !defined( __ANDROID__ ) // server Linux
    // A memory backed view that spans at least one PMD grows to end on a PMD
    // boundary: the kernel faults a huge page in only where the whole aligned
    // pmd_span lies inside the mapping, so a view ending mid-span backs that
    // last span with small pages - which stay small once the next growth
    // covers the rest of it (only khugepaged would collapse them). The extra
    // tail is untouched address space (and, for a memfd, a sparse length).
    // File backed views are left exact: their length is the file's (a
    // private view grows with anonymous memory, so it is not).
    if ( ( !mapping_.is_file_based() || private_view ) && ( target_capacity >= detail::pmd_span ) )
    {
        // (A private view grows in place or relocates as a whole, keeping its
        // phase: it never takes the move into its file's phase.)
        auto const phase{ detail::pmd_phase_after_growth( view_.data(), mapped_size(), target_capacity, private_view ? -1 : mapping_.get() ) };
        target_capacity = align_up( phase + target_capacity, detail::pmd_span ) - phase;
    }
#endif
    if ( private_view ) [[ unlikely ]]
    {
        if ( grow_privately( target_capacity ) ) [[ likely ]]
            return data();
        move_into_memory( mapped_size() );
    }
    // Exact-size expansion only. Geometric growth is the vector's responsibility.
    auto const current_fc_capacity{ storage_size() };
    if ( current_fc_capacity < target_capacity ) [[ unlikely ]]
        set_size( mapping_, file_length_for( target_capacity ) );
#if PSI_VM_HUGE_PAGE_MAX_COVERAGE && defined( __linux__ ) && !defined( __ANDROID__ )
    auto const populated_size{ mapped_size() };
    auto * const data{ expand_view( target_capacity ) };
    // Storage that asked for huge pages but started below a PMD span has
    // just reached one: advise it now (see map_memory()).
    if ( huge_pages_ && ( populated_size < detail::pmd_span ) && ( target_capacity >= detail::pmd_span ) )
        advise_huge_pages( populated_size );
    return data;
#else
    return expand_view( target_capacity );
#endif
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
    // A private view never resizes what it views (see move_into_memory()).
    if ( views_privately() ) [[ unlikely ]]
    {
        shrink_privately( storage_size );
        return data();
    }
    auto const current_file_length{ this->storage_size() };
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

PSI_COLD
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

#if PSI_VM_HUGE_PAGE_MAX_COVERAGE && defined( __linux__ ) && !defined( __ANDROID__ )
    huge_pages_ = false;
#endif
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
PSI_COLD
err::result_or_error<void, error> mem_mapping::map_memory( size_type const data_size, header_info const hdr_info, huge_pages const huge ) noexcept
{
    auto hdr{ unpack( hdr_info ) };
    auto map_success{ map( {}, memory_storage_size( hdr.total_hdr_size() + data_size, huge ) ) };
    if ( !map_success )
        return map_success.error();
#if PSI_VM_HUGE_PAGE_MAX_COVERAGE && defined( __linux__ ) && !defined( __ANDROID__ )
    huge_pages_ = ( huge == huge_pages::yes );
#endif
    if ( huge == huge_pages::yes )
        advise_huge_pages( 0 ); // before anything is written
    hdr.data_size = data_size;
    get_sizes() = hdr;
    live_size_  = data_size; // anonymous storage: nothing to commit to, live == committed
    return err::success;
}

PSI_COLD
err::result_or_error<void, error> mem_mapping::map_cow_memory( size_type const data_size, header_info const hdr_info, huge_pages const huge ) noexcept
{
#ifdef __linux__
    // On Linux, create a memfd-backed mapping so that future COW copies (via
    // the copy constructor) are zero-copy: dup(fd) + MAP_PRIVATE, instead of
    // requiring an initial memcpy into a memfd. The memfd is anonymous but
    // fd-backed, so it participates in the regular file-backed COW path.
    auto const total_size{ memory_storage_size( unpack( hdr_info ).total_hdr_size() + data_size, huge ) };
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
#if PSI_VM_HUGE_PAGE_MAX_COVERAGE && defined( __linux__ ) && !defined( __ANDROID__ )
            huge_pages_ = ( huge == huge_pages::yes );
#endif
            if ( huge == huge_pages::yes )
                advise_huge_pages( 0 ); // before anything is written
            hdr.data_size = data_size;
            get_sizes() = hdr;
            live_size_  = data_size; // ephemeral storage: live == committed
            return err::success;
        }
        ::close( mfd );
    }
    // memfd_create or ftruncate failed -- fall back to regular anonymous
#endif // __linux__
    return map_memory( data_size, hdr_info, huge );
}

mem_mapping::size_type mem_mapping::memory_storage_size( size_type const storage_size, [[ maybe_unused ]] huge_pages const huge ) noexcept
{
#if PSI_VM_HUGE_PAGE_MAX_COVERAGE && defined( __linux__ ) && !defined( __ANDROID__ )
    // A huge page is only ever mapped where the whole PMD span lies inside
    // the mapping, so storage that asked for them spans whole PMDs: the last
    // span gets a huge page too, at the cost of up to one span of capacity
    // the storage may never use (and, on kernels that allocate a huge folio
    // for a partly covered memfd span, would pay for anyway).
    if ( ( huge == huge_pages::yes ) && ( storage_size >= detail::pmd_span ) )
        return align_up( storage_size, detail::pmd_span );
#endif
    return storage_size;
}

bool mem_mapping::can_release_pages( [[ maybe_unused ]] bool const shared ) const noexcept
{
    if ( !has_attached_storage() || mapping_.is_file_based() )
        return false;
    [[ maybe_unused ]] auto const clone_view{ mapping_.view_mapping_flags.is_cow() };
#if defined( __linux__ )
    // A private view (map_memory(), and every COW clone: a MAP_PRIVATE view of
    // the source's memfd) drops only its own pages.  The shared memfd view of
    // map_cow_memory() has to punch its pages out of the memfd itself (for it
    // MADV_DONTNEED frees nothing: the pages stay in the memfd) - and a clone
    // reads the pages it has not copied yet straight from the memfd, so they
    // would read back as zeros: only while no clone is alive.
    return clone_view || !shared;
#elif defined( _WIN32 )
    // The pool is a pagefile backed section.  A clone's copy-on-write view
    // resets only the pages it has copied: the section's own pages stay dirty.
    // The read-write view resets the section's pages, which a clone that has
    // not copied them yet reads, and would then lose once the OS repurposes
    // them: only while no clone is alive.
    return clone_view || !shared;
#elif defined( __APPLE__ )
    // A clone is a mach_vm_remap() copy of the source's shared anonymous
    // memory, and while the two share it the kernel takes MADV_FREE_REUSABLE
    // from either side without freeing anything: not a hazard, just a
    // wasted call.
    return !clone_view && !shared;
#else
    return false;
#endif
}

bool mem_mapping::release_pages( std::byte * const first, size_type const size, bool const shared ) noexcept
{
    BOOST_ASSERT( is_aligned( first, page_size ) );
    BOOST_ASSERT( size % page_size == 0 );
    BOOST_ASSERT( ( first >= view_.data() ) && ( first + size <= view_.data() + view_.size() ) );
    if ( !size || !can_release_pages( shared ) )
        return false;
#if defined( __linux__ )
    return ::madvise( first, size, mapping_.view_mapping_flags.is_cow() ? MADV_DONTNEED : MADV_REMOVE ) == 0;
#elif defined( __APPLE__ )
    return ::madvise( first, size, MADV_FREE_REUSABLE ) == 0;
#elif defined( _WIN32 )
    // MEM_RESET marks the contents disposable and VirtualUnlock, on pages that
    // were never locked, takes them out of the working set: they go to the
    // standby list clean, for the OS to repurpose without writing them to the
    // pagefile.  DiscardVirtualMemory does the same (to the free list) for
    // ~15 us per call rather than ~1-2 us.
    if ( !::VirtualAlloc( first, size, MEM_RESET, PAGE_READWRITE ) )
        return false;
    ::VirtualUnlock( first, size ); // 'fails' with ERROR_NOT_LOCKED, as documented for this use
    return true;
#else
    return false;
#endif
}

void mem_mapping::reuse_pages( [[ maybe_unused ]] std::byte * const first, [[ maybe_unused ]] size_type const size ) noexcept
{
#if defined( __APPLE__ )
    // Without it a page released with MADV_FREE_REUSABLE stays out of the
    // task's footprint however much it is written again.
    BOOST_ASSERT( is_aligned( first, page_size ) );
    ::madvise( first, size, MADV_FREE_REUSE );
#endif
}

PSI_COLD
void mem_mapping::advise_huge_pages( [[ maybe_unused ]] size_type const populated_size ) noexcept
{
#if defined( __linux__ ) && !defined( __ANDROID__ ) // server Linux
    if ( view_.size() < detail::pmd_span )
        return;
    // Only hints, and the kernel decides per backing whether they apply, so
    // their results are not checked (a kernel built without THP rejects them
    // with EINVAL, one older than 6.1 does not know MADV_COLLAPSE).
    (void)::madvise( view_.data(), view_.size(), MADV_HUGEPAGE );
#   if PSI_VM_HUGE_PAGE_MAX_COVERAGE
    // Storage that reached its first PMD span by growing already holds small
    // pages there, and they would stay small (khugepaged may collapse them,
    // eventually, or never): collapse them now, synchronously - a copy of up
    // to one span, once per storage.
    auto const begin{ reinterpret_cast<std::uintptr_t>( view_.data() ) };
    auto const first{ align_up  ( begin                                  , detail::pmd_span ) };
    auto const last { align_down( begin + std::min( view_.size(), align_up( populated_size, detail::pmd_span ) ), detail::pmd_span ) };
    if ( populated_size && ( last > first ) )
        (void)::madvise( reinterpret_cast<void *>( first ), last - first, MADV_COLLAPSE );
#   endif
#endif
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
