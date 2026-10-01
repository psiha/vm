////////////////////////////////////////////////////////////////////////////////
/// VM-backed storage
///
/// Provides mem_mapping (untyped, byte-level VM mapping management) and
/// vm_storage<T> (typed wrapper with storage_* interface for vector<>).
///
/// mem_mapping manages memory-mapped (VM) regions with optional file
/// backing, COW copy semantics, and header support for persistent containers.
///
/// vm_storage<T> wraps mem_mapping with T-typed size/pointer conversion
/// and provides the storage_* interface that vector<Storage> expects.
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
#pragma once

#include <psi/vm/containers/vector.hpp>
#include <psi/vm/mapping/mapping.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>
#include <psi/vm/mappable_objects/file/file.hpp>
#include <psi/vm/mappable_objects/file/utility.hpp>

#include <psi/build/attributes.hpp>
#include <psi/build/datasizeof.hpp>
#include <psi/build/disable_warnings.hpp>

#include <boost/assert.hpp>

#include <cstddef>
#include <span>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// vm_vector: an optionally persistent container class template which uses VM/a
// mapped object for its backing storage. Currently limited to
// trivially_moveable types (even when backed by RAM/temporal storage).
//
// Allowing for a header to store/persist the 'actual' size of the container can
// be generalized to storing arbitrary (types of) headers. This however makes
// this class template no longer model just a vector-like container but rather a
// structure consisting of:
//  - a fixed-sized part (the header) and
//  - a dynamically resizable part (the vector part)
// ...kind of like the 'curiously recurring C pattern' of a struct with a
// zero-sized trailing array data member.
// Awaiting a better name for the idiom, considering its usefulness, the library
// exposes this ability/functionality publicly - as a runtime parameter (instead
// of a Header template type parameter) - the relative runtime cost should be
// near non-existent vs all the standard benefits (less template instantiations
// and codegen copies plus having concrete storage_t classes, enabling
// hierarchies of types and their respective headers - implemented through the
// functionality provided the header_info class).
//
// Checkout a version prior to October 2025 for a version:
// * Offering a 'headerless' type of vm_vector - this was abandoned as the added
//   complexity does not seem to be worth it considering how close such a type
//   is to a plain file mapping (i.e. little added value).
// * Storing the (data) size at the _end_ of the header, which has the benefit
//   of minimizing alignment slack, giving the header automatic maximum/commit
//   granularity alignment and enabling client code to write at the very
//   beginning of the file (e.g. for writing out magic FourCCs). This was also
//   abandoned as it, besides being more complex, disables the library from
//   automatically performing basic validity/corruption checks upon mapping a
//   file.
// * Offering compile time parameterization of the type used for physical
//   storage of the (data) size - this was also abandoned with the previous
//   point in favour a simpler approach, with a concrete sizes_hdr type which
//   unconditionally uses std::size_t while limitting the header size to 32 bits
//   (and using the slack space between the 32bit data_offset and 64bit
//   data_size to store/cache both the client header size and offset).
// * Accepted and stored the header_info in its constructor - this approach was
//   simpler as it handled that logic in a single place (construction, as
//   opposed to the current approach where it has to be passed in each mapping/
//   opening API which can especially proliferate in more complex user code) but
//   it required storing the header size as a data member and was less
//   versatile for scenarios like object pools where you'd want to reuse a
//   vm_vector object as the underlying container for a different type of
//   persisted object (which has a different header type) or having a container
//   of effectively polymorphic persisted types (where obviously you have to
//   specify the header layout for individual objects after the creation of the
//   container or even 'discover' it after opening from disk). The new approach
//   where the header info is provided late, on storage mapping/opening
//   operations is more verbose but handles said situations better.
//
// Checkout a revision prior to March the 21st 2024 for a version that used
// statically sized header sizes.

struct header_info
{
    using align_t = std::uint16_t; // fit page_size

    // Mitigation for alignment codegen being sprayed allover at header_data
    // call sites - allow it to assume a small, 'good enough for most',
    // guaranteed alignment (event at the possible expense of slack space in
    // header hierarchies) so that alignment fixups can be skipped for such
    // headers.
    static std::uint8_t constexpr minimal_subheader_alignment{ alignof( int ) };
    static std::uint8_t constexpr minimal_data_alignment     { 32 };

    // note: any potential benefit of __datasizeof (vs sizeof) is nullified with
    // the use of alignof( T ) - TODO revise/expand on this as needed

    constexpr header_info() = default;
    constexpr header_info( std::uint32_t const _size, std::uint8_t const _alignment, bool const _extendable = false, align_t const _data_extra_alignment = minimal_data_alignment ) noexcept
        : size{ _size }, alignment{ std::max( _alignment, minimal_subheader_alignment ) }, extendable{ _extendable }, data_extra_alignment{ _data_extra_alignment }
    {}

    template <typename T>
    constexpr header_info( std::in_place_type_t<T>, bool const _extendable = false ) noexcept : header_info{ __datasizeof( T ), alignof( T ), _extendable } {}

    template <typename AdditionalHeader>
    constexpr header_info add_header( bool const _extendable = false ) const noexcept // support chained headers (class hierarchies)
    {
        auto const subheader_alignment{ std::max<std::uint8_t>( alignof( AdditionalHeader ), minimal_subheader_alignment ) };
        // Pad the prepended header to the TAIL stack's alignment as well as its
        // own: header_data<Tail>() aligns the tail's base up to alignof(Tail)
        // at retrieval time, so if the prepended header's padded size is not a
        // multiple of that alignment (a) the total size computed here comes up
        // short by up to final_alignment()-minimal_subheader_alignment bytes,
        // and (b) any caller that advances past the prepended header manually
        // (raw subspan arithmetic) lands at a different offset than
        // header_data's aligned one - a silent split between two views of the
        // same header stack. Padding to max(own, tail) makes the retrieval-time
        // fixup a no-op by construction and the size exact.
        auto const padded_size        { align_up( __datasizeof( AdditionalHeader ), std::max( subheader_alignment, final_alignment() ) ) };
        return
        {
            static_cast<std::uint32_t>( padded_size + this->size ),
            std::max( final_alignment(), subheader_alignment ),
            this->extendable || _extendable,
            this->data_extra_alignment
        };
    }

    constexpr header_info with_final_alignment( align_t const data_alignment ) const noexcept
    {
        BOOST_ASSUME( data_extra_alignment == minimal_data_alignment ); // already set?
        auto aligned{ *this };
        aligned.data_extra_alignment = std::max<align_t>( data_alignment, minimal_data_alignment );
        return aligned;
    }
    template <typename T>
    constexpr header_info with_final_alignment_for() const noexcept { return with_final_alignment( alignof( T ) ); }

    constexpr std::uint32_t final_header_size() const noexcept { return align_up( size, final_alignment() ); }
    constexpr std::uint8_t  final_alignment  () const noexcept
    {
        BOOST_ASSUME( alignment >= minimal_subheader_alignment );
        auto const is_pow2{ std::has_single_bit( alignment ) };
        BOOST_ASSUME( is_pow2 );
        return alignment;
    }

    explicit operator bool() const noexcept { return size != 0; }

    std::uint32_t size     { 0 };
    std::uint8_t  alignment{ minimal_subheader_alignment };
    bool          extendable{ false }; // inverse of the cpp 'final' keyword - whether the header is allowed to be extended with additional data (e.g. in derived types)
    align_t       data_extra_alignment{ minimal_data_alignment }; // e.g. for vectorization or overlaying complex types over std::byte storage
}; // header_info

// utility function for extracting 'sub-header' data (i.e. intermediate headers
// in an inheritance hierarchy)
template <typename Header>
[[ gnu::const ]] auto header_data( std::span<std::byte> const hdr_storage ) noexcept
{
    auto const in_alignment{ header_info::minimal_subheader_alignment };
    if constexpr ( alignof( Header ) <= in_alignment ) // even with all the assume hints Clang v18 still cannot eliminate redundant fixups so we have to do it explicitly
    {
        BOOST_ASSERT( hdr_storage.size() >= __datasizeof( Header ) );
        return std::pair
        {
            reinterpret_cast<Header *>( hdr_storage.data() ),
            hdr_storage.subspan( align_up<in_alignment>( __datasizeof( Header ) ) )
        };
    }
    else
    {
        auto const     raw_data { std::assume_aligned<in_alignment>( hdr_storage.data() ) };
        auto const aligned_data { align_up<alignof( Header )>( raw_data ) };
        auto const aligned_space{ static_cast<std::uint32_t>( hdr_storage.size() ) - unsigned( aligned_data - raw_data ) };
        BOOST_ASSUME( aligned_space >= __datasizeof( Header ) );
        return std::pair
        {
            reinterpret_cast<Header *>( aligned_data ),
            std::span{ align_up<in_alignment>( aligned_data + __datasizeof( Header ) ), aligned_space - __datasizeof( Header ) }
        };
    }
} // header_data()
template <typename Header>
[[ gnu::const ]] auto header_data( std::span<std::byte const> const hdr_storage ) noexcept
{
    auto const mutable_data{ header_data<Header>( std::bit_cast<std::span<std::byte>>( hdr_storage ) ) };
    return std::pair{ mutable_data.first, std::bit_cast<std::span<std::byte const>>( mutable_data.second ) };
}


//! Whether memory backed storage asks for transparent huge pages (see
//! mem_mapping::map_memory() and doc/huge_pages.md).
enum class huge_pages : bool { no, yes };

// -DPSI_VM_HUGE_PAGE_MAX_COVERAGE=1: storage that asks for huge pages is also
// sized in whole PMD spans, advised when it first grows into a PMD span, and
// that span collapsed (see map_memory() and doc/huge_pages.md). Off by
// default: it buys coverage with memory, and it adds a member to mem_mapping
// and a check to every growth, so it must be defined for the whole program.
#ifndef PSI_VM_HUGE_PAGE_MAX_COVERAGE
#   define PSI_VM_HUGE_PAGE_MAX_COVERAGE 0
#endif

PSI_WARNING_DISABLE_PUSH()
PSI_WARNING_CLANGCL_DISABLE( -Wignored-attributes )

class [[ clang::trivial_abi ]] mem_mapping
{
public:
    using value_type = std::byte;
    using  size_type = std::size_t;

    mem_mapping( mem_mapping && ) = default;
    mem_mapping & operator=( mem_mapping && ) = default;

    // Write-through of the live length on destruction, so an orderly teardown
    // leaves the mapping's header describing what the container actually
    // spans. This is a store into an already-mapped page - deliberately NOT a
    // flush: forcing a syscall on destruction is the user's prerogative, never
    // this type's. The body runs while view_/mapping_ are still alive; on a
    // moved-from object has_attached_storage() is false and publish_size() is
    // a no-op. (Move operations are user-declared above, so declaring this
    // does not suppress them.)
    ~mem_mapping() noexcept { publish_size(); }


    // COW (copy-on-write) copy construction: creates a new storage sharing
    // physical pages with the source. Writes to the clone trigger private
    // page copies (kernel-managed COW).
    // - File-backed (all platforms): MAP_PRIVATE / PAGE_WRITECOPY view
    // - Anonymous (Windows): WRITECOPY view of the same pagefile section
    // - Anonymous (macOS): mach_vm_remap(copy=TRUE) + deep-copy fallback
    // - Anonymous (Linux): a copy into a memfd of the clone's own, mapped as
    //   map_cow_memory() maps one (so clones of the clone are true COW), or a
    //   plain deep copy
    // A clone never resizes the object it shares with the source (a file, a
    // memfd or a section), nor maps more of it than it was created with: it
    // grows past it with memory of its own, the untouched pages staying
    // shared.
    // A clone of a clone copies what the source clone shows: its private
    // writes are in no object a second view could map.
    explicit mem_mapping( mem_mapping const & );

    [[ gnu::pure ]] size_type header_size() const noexcept { return get_sizes().client_hdr_size(); }

    [[ gnu::pure, nodiscard ]] std::span<std::byte const> header_storage() const noexcept { return const_cast<mem_mapping &>( *this ).header_storage(); }
    [[ gnu::pure, nodiscard ]] std::span<std::byte      > header_storage()       noexcept;

    // Unattached storage (default constructed, closed or moved from) reads as
    // empty: no data, no size, no capacity. Its first growth (or reserve())
    // attaches anonymous memory.

    //! The live length: what the container currently spans, including growth
    //! not yet committed. Reads a plain member - no mapped-page touch - which
    //! is zero whenever no storage is attached.
    [[ nodiscard, gnu::pure ]] size_type size       () const noexcept { return live_size_; }
    //! The length recorded in the persisted header, i.e. the extent as of the
    //! last header-covering flush (or clean detach). After an abnormal
    //! termination this is what a reopen reports as size(); everything
    //! physically beyond it is uncommitted by construction. Equals size()
    //! outside an in-flight mutation.
    [[ nodiscard, gnu::pure ]] size_type committed_size() const noexcept { return has_attached_storage() ? get_sizes().data_size : size_type{ 0 }; }
    [[ nodiscard, gnu::pure ]] size_type fs_capacity() const noexcept { return storage_size() - get_sizes().data_offset; }
    [[ nodiscard, gnu::pure ]] size_type vm_capacity() const noexcept { return has_attached_storage() ? attached_vm_capacity() : size_type{ 0 }; }

    //! The start of the data, or null while no storage is attached.
    [[ nodiscard, gnu::pure ]] value_type       * data()       noexcept { return has_attached_storage() ? attached_data() : nullptr; }
    [[ nodiscard, gnu::pure ]] value_type const * data() const noexcept { return const_cast<mem_mapping &>( *this ).data(); }

    //! <b>Effects</b>: Returns true if the vector contains no elements.
    //! <b>Throws</b>: Nothing.
    //! <b>Complexity</b>: Constant.
    [[ nodiscard, gnu::pure ]] bool empty() const noexcept { return !size(); }

    //! <b>Effects</b>: Tries to deallocate the excess of memory created
    //!   with previous allocations. The size of the vector is unchanged.
    //!   For file-backed storage the on-disk length is kept rounded up to a
    //!   page, so up to one page of slack can survive the call (the trade that
    //!   keeps an extending resize off the dirty-tail-block flush path - see
    //!   file_length_for in vm_vector.cpp).
    //!
    //! <b>Throws</b>: nothing.
    //!
    //! <b>Complexity</b>: Constant.
    void shrink_to_fit() noexcept;

    void unmap() noexcept { view_.unmap(); }

    void close() noexcept;

    [[ nodiscard, gnu::pure ]] size_type storage_size() const noexcept { return get_size( mapping_ ); }
    [[ nodiscard, gnu::pure ]] size_type  mapped_size() const noexcept { return view_.size(); }

    void                              flush_async   ()       noexcept {        flush_async   ( 0, mapped_size() ); }
    err::fallible_result<void, error> flush_blocking()       noexcept { return flush_blocking( 0, mapped_size() ); }

    //! Publishes the live length into the mapped header, making it the value
    //! a subsequent reopen will report. NOTE: this is a store into a mapped
    //! page - it is NOT durability. To make the published length durable,
    //! follow it with a header-covering flush (flush_blocking()/(0, n)).
    //! What it buys without a flush is still the essential part: the header
    //! can only ever hold a value the caller published at a commit point,
    //! never an in-flight cursor, so whatever the page cache writes back is
    //! by construction a committed extent.
    //! Skips the store when nothing changed, to avoid dirtying the page.
    void publish_size() noexcept;

    // A flush whose range starts at 0 covers the header, so it publishes the
    // live length first (it would otherwise flush a stale one). Data-only
    // flushes (beginning != 0) deliberately do not - a length must never
    // become durable before the bytes it spans.
    void                              flush_async   ( size_type beginning, size_type size )       noexcept;
    err::fallible_result<void, error> flush_blocking( size_type beginning, size_type size )       noexcept;

    [[ nodiscard, gnu::pure ]] bool file_backed() const noexcept { return mapping_.is_file_based(); }

    [[ nodiscard, gnu::pure ]] bool has_attached_storage() const noexcept { return static_cast<bool>( mapping_ ); }

    auto underlying_file() const noexcept { return mapping_.underlying_file(); }

    err::fallible_result<void, error>
    map_file( auto const * const file_name, flags::named_object_construction_policy const policy, header_info const hdr_info ) noexcept
    {
        return map_file
        (
            create_file( file_name, create_rw_file_flags( policy ) ),
            policy,
            hdr_info
        );
    }

    // With huge_pages::yes, storage that spans at least one PMD (2 MiB with
    // the 4 KiB granule) when mapped is advised to be backed by transparent
    // huge pages (Linux MADV_HUGEPAGE; ignored on every other platform). The
    // kernel weighs the hint per backing: private anonymous (map_memory)
    // storage follows transparent_hugepage/enabled, memfd (map_cow_memory)
    // storage follows transparent_hugepage/shmem_enabled.
    // The advice is given here, before this function (the sizes header) or
    // the container (the initial elements) writes anything: the kernel
    // decides a page's size when the page is first touched, and a small page
    // keeps the whole PMD span around it small. The advice is a property of
    // the kernel's VMA, so growth (mremap, in place or moved) keeps it.
    // Smaller storage is not advised: some kernels (6.6) allocate a whole
    // huge folio for a memfd span the mapping covers only partly.
    // huge_pages::no (the default) costs nothing. What each option costs and
    // when it pays: doc/huge_pages.md.
    err::result_or_error<void, error> map_memory    ( size_type data_size, header_info, huge_pages = huge_pages::no ) noexcept;
    // Like map_memory but on Linux creates a memfd-backed mapping so that
    // future COW copies (via copy constructor) are zero-copy dup+MAP_PRIVATE
    // instead of requiring an initial memcpy. On other platforms this is
    // identical to map_memory. COW copies are never advised: a copy's writes
    // copy into small pages whatever the advice.
    err::result_or_error<void, error> map_cow_memory( size_type data_size, header_info, huge_pages = huge_pages::no ) noexcept;

    // Hand whole pages of the view back to the OS without unmapping them: they
    // stop counting as resident and their contents become unspecified - zeros,
    // the source's current bytes for a Linux clone, or the old bytes where the
    // OS has not repurposed them yet.  'shared' says whether another view may
    // still be reading these pages (a COW clone of this storage, or the storage
    // this one is a clone of).  Returns whether the pages were released:
    //  * file backed storage: never - a file's pages are its data, and hole
    //    punching a persisted file is not this call's to decide;
    //  * a private view (Linux map_memory()) and a COW clone's view (Linux,
    //    Windows): only the view's own copies - MADV_DONTNEED, MEM_RESET;
    //  * a shared view (the memfd behind Linux map_cow_memory(), the pagefile
    //    section behind Windows storage, macOS anonymous shared memory): the
    //    pages themselves - MADV_REMOVE, MEM_RESET - so only while no clone
    //    reads them.  macOS: MADV_FREE_REUSABLE, which the kernel ignores
    //    while the memory is shared with a clone (from either side, so it is
    //    never a hazard there - and a clone's view does not try).
    // A huge page is released whole or not at all: see release_granularity().
    // The cost is a system call per range, and on Windows more than one (see
    // the ranges overload): a caller with many pages to release hands them
    // over all at once, coalesced into runs.
    bool release_pages( std::byte * first, size_type size, bool shared ) noexcept;
    // Several ranges (each page aligned, a whole number of pages) in one go.
    // Linux: one process_madvise() for all of them (up to IOV_MAX a call)
    // where the kernel takes this advice for the calling process (6.13 on),
    // else madvise() per range; macOS: madvise() per range; Windows: a
    // MEM_RESET per range and one working set call for all of them.  Whether
    // the one-call form works is found out by trying it: the first refusal
    // (EINVAL, ENOSYS, an unknown information class...) switches every
    // mem_mapping to the per range calls for good.  Returns whether all the
    // ranges were released.
    struct page_range { std::byte * first; size_type size; }; // (the layout of the NT MEMORY_RANGE_ENTRY, and of iovec)
    bool release_pages( std::span<page_range const> ranges, bool shared ) noexcept;
    // Whether that takes fewer calls than ranges: the OS has not refused the
    // one-call form (yet), and it is not switched off - which
    // allow_release_ranges_at_once( false ) does, for every mem_mapping (for
    // diagnostics, and to test the per range path where the OS has both).
    [[ nodiscard ]] static bool release_takes_ranges_at_once() noexcept;
    static void allow_release_ranges_at_once( bool allowed ) noexcept;
    // Whether release_pages() would release anything from this storage.
    [[ nodiscard ]] bool can_release_pages( bool shared ) const noexcept;
    // The unit to hand to release_pages() whole, and aligned to by address:
    // a PMD span (2 MiB with 4 KiB pages) for storage that asked for huge
    // pages, else a page. Releasing part of a huge page does not return its
    // memory: the kernel splits the huge mapping, and frees the rest of the
    // huge page only when it later splits the page itself under memory
    // pressure - or, for a memfd, only zeroes the range where that split
    // fails - while khugepaged may collapse the span again, faulting the
    // released pages back in. Known only under PSI_VM_HUGE_PAGE_MAX_COVERAGE
    // (elsewhere this is a page, and a huge page backed release is partial);
    // not known either way for storage that the system's transparent huge
    // page policy ("always") backs with huge pages without being asked.
    [[ nodiscard ]] size_type release_granularity() const noexcept;
    // Released pages about to be written again (macOS: MADV_FREE_REUSE, so
    // they count toward the footprint again; nothing elsewhere).
    void reuse_pages( std::byte * first, size_type size ) noexcept;

    explicit operator bool() const noexcept { return has_attached_storage(); }

protected:
    static bool constexpr storage_zero_initialized{ true };

    struct sizes_hdr
    {
        std::uint32_t data_offset;
        std::uint32_t hdr_size   : 24;
        std::uint32_t hdr_offset :  8;
        size_type     data_size;

        std::uint32_t client_hdr_size() const noexcept
        {
            auto const deduced_size{ data_offset - hdr_offset };
            auto const  cached_size{ hdr_size };
            BOOST_ASSUME( deduced_size == cached_size );
            return cached_size;
        }
        std::uint32_t total_hdr_size() const noexcept { BOOST_ASSUME( data_offset % header_info::minimal_data_alignment == 0 ); return data_offset; }
    }; // struct sizes_hdr
    static_assert( sizeof( sizes_hdr ) == 2 * sizeof( void * ) );

    constexpr mem_mapping() = default;

    [[ nodiscard, gnu::pure, gnu::assume_aligned( reserve_granularity ) ]] value_type       * mapped_data()       noexcept { BOOST_ASSERT_MSG( mapping_, "Backing storage not attached" ); return std::assume_aligned<commit_granularity>( view_.data() ); }
    [[ nodiscard, gnu::pure, gnu::assume_aligned( reserve_granularity ) ]] value_type const * mapped_data() const noexcept { return const_cast<mem_mapping &>( *this ).mapped_data(); }

    // Attached storage only (unchecked).
    [[ nodiscard, gnu::pure, gnu::assume_aligned( header_info::minimal_data_alignment ) ]]
    value_type * attached_data() noexcept
    {
        return std::assume_aligned<header_info::minimal_data_alignment>( mapped_data() + get_sizes().data_offset );
    }
    [[ nodiscard, gnu::pure, gnu::assume_aligned( header_info::minimal_data_alignment ) ]]
    value_type const * attached_data() const noexcept { return const_cast<mem_mapping &>( *this ).attached_data(); }
    [[ nodiscard, gnu::pure ]] size_type attached_vm_capacity() const noexcept { return mapped_size() - get_sizes().data_offset; }

    sizes_hdr       & get_sizes()       noexcept { return *reinterpret_cast<sizes_hdr       *>( mapped_data() ); }
    sizes_hdr const & get_sizes() const noexcept { return *reinterpret_cast<sizes_hdr const *>( mapped_data() ); }

    // template (char type) independent portion of map_file
    PSI_COLD
    err::result_or_error<void, error>
    map_file( file_handle file, flags::named_object_construction_policy, header_info ) noexcept;

    void swap( mem_mapping & other ) noexcept { std::swap( *this, other ); }

    // Attaches anonymous memory first when no storage is attached yet, its
    // data aligned to data_alignment.
    void   reserve              ( size_type new_capacity, header_info::align_t data_alignment = header_info::minimal_data_alignment );
    void * shrink_to_slow       ( size_type target_size ) noexcept( mapping::views_downsizeable );
    void * expand_view          ( size_type target_size );
    void   shrink_mapped_size_to( size_type target_size ) noexcept( mapping::views_downsizeable );

    template <geometric_growth G = geometric_growth{1, 1}>
    void * grow_to( size_type const byte_target, header_info::align_t const data_alignment = header_info::minimal_data_alignment )
    {
        if ( byte_target > size() ) [[ likely ]]
        {
            auto const byte_cap{ vm_capacity() }; // zero while unattached: reserve() attaches
            if ( byte_target > byte_cap ) [[ unlikely ]]
                reserve( static_cast<bool>( G ) ? G( byte_target, byte_cap ) : byte_target, data_alignment );
            live_size() = byte_target;
            return attached_data();
        }
        return data();
    }

    void * shrink_to( size_type target_size ) noexcept;

    void resize( size_type target_size );

    bool has_extra_capacity() const noexcept
    {
        BOOST_ASSERT( size() <= vm_capacity() );
        return size() != vm_capacity();
    }

    void grow_into_available_capacity_by( size_type const sz_delta ) noexcept
    {
        BOOST_ASSERT_MSG( sz_delta <= ( vm_capacity() - size() ), "Out of preallocated space" );
        live_size() += sz_delta;
    }

    void shrink_size_to( size_type const new_size ) noexcept
    {
        BOOST_ASSUME( new_size <= vm_capacity() );
        live_size() = new_size;
    }

private:
    err::result_or_error<void, error>
    map( file_handle file, std::size_t mapping_size ) noexcept;

    static constexpr sizes_hdr unpack( header_info ) noexcept;

    //! Mutable access to the LIVE length (the in-flight one). Every internal
    //! growth/shrink path moves this and only this - publishing to the header
    //! is the caller's explicit act (publish_size()).
    [[ nodiscard, gnu::pure ]] size_type & live_size() noexcept { return live_size_.value; }
    //! Mutable access to the PERSISTED length in the mapped header. Only
    //! publish_size() may move it.
    [[ nodiscard, gnu::pure ]] size_type & persisted_size() noexcept { return get_sizes().data_size; }

    void * expand_capacity( size_type target_storage_capacity );

    //! Whether this is a private (copy-on-write) view of a mappable object
    //! (file, memfd or section), i.e. a COW clone: the object is shared with
    //! the source, so it is not the clone's to resize - nor does it hold the
    //! clone's own writes.
    [[ nodiscard, gnu::pure ]] bool views_privately() const noexcept;
    //! Growth and shrinkage of a private view: the object part stays mapped
    //! (and shared with the source) as it is, the view grows past it with
    //! anonymous memory of the clone's own, and neither ever resizes the
    //! object. grow_privately() returns false where it cannot (no address
    //! space to grow or move into), leaving the view as it was.
    [[ nodiscard ]] bool grow_privately  ( size_type target_size );
    void                 shrink_privately( size_type target_size ) noexcept( mapping::views_downsizeable );
    void move_into_memory( size_type mapped_size );

    //! Linux: the storage size to map for storage_size bytes (whole PMD spans
    //! for huge page backed storage under PSI_VM_HUGE_PAGE_MAX_COVERAGE).
    static size_type memory_storage_size( size_type storage_size, huge_pages ) noexcept;
    //! Linux: advises the view MADV_HUGEPAGE if it spans a PMD (and, under
    //! PSI_VM_HUGE_PAGE_MAX_COVERAGE, collapses the whole PMD spans that
    //! already hold the first populated_size bytes in small pages).
    void advise_huge_pages( size_type populated_size ) noexcept;

    size_type client_to_storage_size( size_type sz ) const noexcept;

private:
    extendable_mapped_view view_;
    mapping                mapping_;
    // The live (in-flight) length. The persisted copy in sizes_hdr::data_size
    // is deliberately NOT updated by growth/shrinkage - see size() and
    // committed_size(). A move hands it over and leaves zero behind, so it is
    // zero whenever no storage is attached and size() needs no check.
    struct [[ clang::trivial_abi ]] live_length
    {
        size_type value{ 0 };

        constexpr live_length() noexcept = default;
        constexpr live_length( live_length const &  ) noexcept = default;
        constexpr live_length( live_length       && other ) noexcept : value{ std::exchange( other.value, size_type{ 0 } ) } {}
        constexpr live_length & operator=( live_length const &  ) noexcept = default;
        constexpr live_length & operator=( live_length       && other ) noexcept { value = std::exchange( other.value, size_type{ 0 } ); return *this; }
        constexpr live_length & operator=( size_type const v ) noexcept { value = v; return *this; }
        constexpr operator size_type() const noexcept { return value; }
    };
    live_length            live_size_;
    // Private views (COW clones) only: how much of the view's address range,
    // from its start, the shared object backs - past it the view is
    // anonymous memory of the clone's own. A length suffices: the view always
    // starts at the object's offset 0 (the storage header, which get_sizes()
    // reads from the view's start, lives there), and it only ever grows or
    // shrinks at its end, or moves whole - never at its front.
    size_type              object_extent_{ 0 };
#if PSI_VM_HUGE_PAGE_MAX_COVERAGE && defined( __linux__ ) && !defined( __ANDROID__ )
    // map_memory()/map_cow_memory() were asked for huge pages (the advice
    // itself is a property of the kernel's VMA, which growth keeps).
    bool                   huge_pages_{ false };
#endif
}; // mem_mapping


// None of the existing or so far introduced traits give information on whether
// a type can reliably be persisted - of the existing ones, only is_fundamental
// is true IFF a type neither is nor contains a pointer or a reference. For the
// rest - specialization-awaiting-standardization...
// Even relative addresses, (fancy) pointers or references, like
// boost::interprocess::offset_ptr, i.e. those that are based on/diffed from an
// absolute address in physical/temporal memory, also cannot be simply
// (trivially) persisted - in effect bitcopied into a different memory space
// where the delta (the relative part of the address) to the target object is
// different (which will generally be the case if the target object is
// dynamically allocated on a private heap or stack). Generic identifiers (IDs
// or names) would be the only type of persistable reference (and by extension
// the only mechanism for creating peristable non-trivial objects, that
// reference other objects or 'resources' in general).
// https://www.youtube.com/watch?v=SGdfPextuAU&t=3829s C++Now 2019: Arthur O'Dwyer "Trivially Relocatable" (persistability thoughts)
template <typename T>
bool constexpr does_not_hold_addresses{ std::is_fundamental_v<T> || std::is_enum_v<T> };


////////////////////////////////////////////////////////////////////////////////
// \class vm_storage
//
// Storage backend for VM-backed (mmap / VirtualAlloc) vectors. Wraps
// mem_mapping with T-typed size/pointer conversion and provides the
// storage_* interface that vector<> expects.
////////////////////////////////////////////////////////////////////////////////

template <typename T, typename sz_t = std::size_t>
requires is_trivially_moveable<T> // TODO relax for memory mappings (should work, only COW 'may move' remappings would need to be disabled)
class [[ clang::trivial_abi ]] vm_storage : public mem_mapping
{
public:
private:
    using base = mem_mapping;

    PSI_WARNING_DISABLE_PUSH()
    PSI_WARNING_GCC_OR_CLANG_DISABLE( -Wsign-conversion )
    static T *  to_t_ptr  ( extendable_mapped_view::value_type * const ptr     ) noexcept {                                             return reinterpret_cast<T *>( ptr ); }
    static sz_t to_t_sz   ( auto                                 const byte_sz ) noexcept { BOOST_ASSUME( byte_sz % sizeof( T ) == 0 ); return static_cast<sz_t>( byte_sz / sizeof( T ) ); }
    // A byte count, in the mapping's own size_type: sz_t counts elements, and
    // a narrow one (a 32 bit counter of 8 byte elements) cannot hold the
    // bytes of every size it can count.
    static base::size_type to_byte_sz( sz_t const sz ) noexcept
    {
        BOOST_ASSERT( sz <= std::numeric_limits<base::size_type>::max() / sizeof( T ) );
        return base::size_type{ sz } * sizeof( T );
    }
    PSI_WARNING_DISABLE_POP()

public:
    using value_type = T;
    using  size_type = sz_t;

    static bool constexpr storage_zero_initialized{ true };

    vm_storage(                           ) = default;
    vm_storage( vm_storage const & other  ) : base{ static_cast<base const &>( other ) } {} // COW copy
    vm_storage( vm_storage       &&       ) = default;
    vm_storage & operator=( vm_storage && ) = default;

    auto map_file( auto const file, flags::named_object_construction_policy const policy, header_info const hdr_info = {} ) noexcept
    requires( does_not_hold_addresses<T> )
    {
        return base::map_file( file, policy, hdr_info.with_final_alignment_for<T>() );
    }

    template <typename InitPolicy = value_init_t>
    err::fallible_result<void, error>
    map_memory( sz_t const initial_data_size = 0, header_info const hdr_info = {}, InitPolicy const init_policy = {}, huge_pages const huge = huge_pages::no ) noexcept
    {
        return construct_fresh( initial_data_size, init_policy, base::map_memory(
            to_byte_sz( initial_data_size ),
            hdr_info.with_final_alignment_for<T>(),
            huge
        ) );
    }

    template <typename InitPolicy = value_init_t>
    err::fallible_result<void, error>
    map_cow_memory( sz_t const initial_data_size = 0, header_info const hdr_info = {}, InitPolicy const init_policy = {}, huge_pages const huge = huge_pages::no ) noexcept
    {
        return construct_fresh( initial_data_size, init_policy, base::map_cow_memory(
            to_byte_sz( initial_data_size ),
            hdr_info.with_final_alignment_for<T>(),
            huge
        ) );
    }

    [[ nodiscard, gnu::pure ]] T       * data()       noexcept { return to_t_ptr( base::data() ); }
    [[ nodiscard, gnu::pure ]] T const * data() const noexcept { return const_cast<vm_storage &>( *this ).data(); }

    [[ nodiscard, gnu::pure ]] sz_t size    () const noexcept { return to_t_sz( base::size() ); }
    //! Element-count counterpart of mem_mapping::committed_size().
    [[ nodiscard, gnu::pure ]] sz_t committed_size() const noexcept { return to_t_sz( base::committed_size() ); }
    [[ nodiscard, gnu::pure ]] sz_t capacity() const noexcept { return static_cast<sz_t>( base::vm_capacity() / sizeof( T ) ); }

    void reserve( sz_t const new_capacity ) { base::reserve( to_byte_sz( new_capacity ), data_alignment ); }
    void reserve( narrowing_size<sz_t> auto ) = delete; // see narrowing_size

    // Compatibility aliases for boost::container::flat_* and generic code
    using allocator_type = std::allocator<T>;
    base const & get_stored_allocator() const noexcept { return *this; }

    decltype( auto ) user_header_data() noexcept { return base::header_storage(); }

    base       & storage_base()       noexcept { return *this; }
    base const & storage_base() const noexcept { return *this; }

    [[ nodiscard, gnu::pure ]] base::size_type mapped_size() const noexcept { return base::mapped_size(); } // bytes

    // --- storage_* interface for vector<> ---
    template <geometric_growth G = geometric_growth{1, 1}>
    T * storage_grow_to  ( sz_t const target_size )          { return static_cast<T *>( base::template grow_to<G>( to_byte_sz( target_size ), data_alignment ) ); }
    T * storage_shrink_to( sz_t const target_size ) noexcept { return static_cast<T *>( base::         shrink_to ( to_byte_sz( target_size ) ) ); }

    // Opt in to vector<>'s shrink_to_fit(): spare capacity here is file
    // length, so it has to be handed back explicitly - storage_shrink_to( size() )
    // is by construction a no-op.
    void storage_shrink_to_fit() noexcept { base::shrink_to_fit(); }

    void storage_shrink_size_to( sz_t const new_size ) noexcept { base::shrink_size_to( to_byte_sz( new_size ) ); }
    void storage_dec_size() noexcept { storage_shrink_size_to( size() - 1 ); }
    void storage_inc_size() noexcept { base::grow_into_available_capacity_by( sizeof( T ) ); }

    void storage_free() noexcept {} // NO-OP: mem_mapping dtor closes the mapping cleanly

private:
    // The alignment map_memory() gives T's data, and so also the storage the
    // first growth attaches.
    static header_info::align_t constexpr data_alignment{ alignof( T ) };

    // Freshly mapped memory reads as zeros, which is a constructed T only for a
    // trivially default constructible one - anything else is constructed here,
    // whichever kind of memory the mapping is.
    template <typename InitPolicy>
    err::fallible_result<void, error> construct_fresh( sz_t const count, InitPolicy, err::result_or_error<void, error> result ) noexcept
    {
        if ( !count || !result || std::is_same_v<InitPolicy, no_init_t> || std::is_trivially_default_constructible_v<T> )
            return result.as_fallible_result();

        auto * const p{ data() };
        if constexpr ( std::is_same_v<InitPolicy, value_init_t> )
            std::uninitialized_value_construct_n( p, count );
        else
            std::uninitialized_default_construct_n( p, count );

        return err::success;
    }
}; // class vm_storage

PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
