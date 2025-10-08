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

#include <psi/vm/containers/vector_impl.hpp>
#include <psi/vm/mapping/mapping.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>
#include <psi/vm/mappable_objects/file/file.hpp>
#include <psi/vm/mappable_objects/file/utility.hpp>

#include <psi/build/datasizeof.hpp>
#include <psi/build/disable_warnings.hpp>

#include <boost/assert.hpp>
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
        auto const padded_size        { align_up( __datasizeof( AdditionalHeader ), subheader_alignment ) };
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
    bool          extendable;
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


PSI_WARNING_DISABLE_PUSH()
PSI_WARNING_CLANGCL_DISABLE( -Wignored-attributes )

class [[ clang::trivial_abi ]] contiguous_storage
{
public:
    using value_type = std::byte;
    using  size_type = std::size_t;

    // TODO shallow/COW copy construction, + OSX vm_copy
    contiguous_storage( contiguous_storage && ) = default;
    contiguous_storage & operator=( contiguous_storage && ) = default;

    [[ gnu::pure ]] size_type header_size() const noexcept { return get_sizes().client_hdr_size(); }

    [[ gnu::pure, nodiscard ]] std::span<std::byte const> header_storage() const noexcept { return const_cast<contiguous_storage &>( *this ).header_storage(); }
    [[ gnu::pure, nodiscard ]] std::span<std::byte      > header_storage()       noexcept;

    [[ nodiscard, gnu::pure ]] size_type size       () const noexcept { return get_sizes().data_size; }
    [[ nodiscard, gnu::pure ]] size_type fs_capacity() const noexcept { return storage_size() - get_sizes().data_offset; }
    [[ nodiscard, gnu::pure ]] size_type vm_capacity() const noexcept { return  mapped_size() - get_sizes().data_offset; }

    //! <b>Effects</b>: Returns true if the vector contains no elements.
    //! <b>Throws</b>: Nothing.
    //! <b>Complexity</b>: Constant.
    [[ nodiscard, gnu::pure ]] bool empty() const noexcept { return !has_attached_storage() || !size(); }

    //! <b>Effects</b>: Tries to deallocate the excess of memory created
    //!   with previous allocations. The size of the vector is unchanged
    //!
    //! <b>Throws</b>: nothing.
    //!
    //! <b>Complexity</b>: Constant.
    void shrink_to_fit() noexcept;

    void unmap() noexcept { view_.unmap(); }

    void close() noexcept;

    [[ nodiscard, gnu::pure ]] size_type storage_size() const noexcept { return get_size( mapping_ ); }
    [[ nodiscard, gnu::pure ]] size_type  mapped_size() const noexcept { return view_.size(); }

    void flush_async   () const noexcept { flush_async   ( 0, mapped_size() ); }
    void flush_blocking() const noexcept { flush_blocking( 0, mapped_size() ); }

    void flush_async   ( size_type beginning, size_type size ) const noexcept;
    void flush_blocking( size_type beginning, size_type size ) const noexcept;

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

    err::result_or_error<void, error> map_memory( size_type data_size, header_info ) noexcept;

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

    constexpr contiguous_storage() = default;

    [[ nodiscard, gnu::pure, gnu::assume_aligned( reserve_granularity ) ]] value_type       * mapped_data()       noexcept { BOOST_ASSERT_MSG( mapping_, "Backing storage not attached" ); return std::assume_aligned<commit_granularity>( view_.data() ); }
    [[ nodiscard, gnu::pure, gnu::assume_aligned( reserve_granularity ) ]] value_type const * mapped_data() const noexcept { return const_cast<contiguous_storage &>( *this ).mapped_data(); }

    [[ nodiscard, gnu::pure, gnu::assume_aligned( header_info::minimal_data_alignment ) ]]
    auto * data( this auto & self ) noexcept
    {
        return std::assume_aligned<header_info::minimal_data_alignment>( self.mapped_data() + self.get_sizes().data_offset );
    }

    sizes_hdr       & get_sizes()       noexcept { return *reinterpret_cast<sizes_hdr       *>( mapped_data() ); }
    sizes_hdr const & get_sizes() const noexcept { return *reinterpret_cast<sizes_hdr const *>( mapped_data() ); }

    // template (char type) independent portion of map_file
    [[ gnu::cold ]]
    err::result_or_error<void, error>
    map_file( file_handle file, flags::named_object_construction_policy, header_info ) noexcept;

    void swap( contiguous_storage & other ) noexcept { std::swap( *this, other ); }

    void   reserve              ( size_type new_capacity );
    void * shrink_to_slow       ( size_type target_size ) noexcept( mapping::views_downsizeable );
    void * expand_view          ( size_type target_size );
    void   shrink_mapped_size_to( size_type target_size ) noexcept( mapping::views_downsizeable );

    void * grow_to( size_type target_size );

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
        stored_size() += sz_delta;
    }

    void shrink_size_to( size_type const new_size ) noexcept
    {
        BOOST_ASSUME( new_size <= vm_capacity() );
        stored_size() = new_size;
    }

private:
    err::result_or_error<void, error>
    map( file_handle file, std::size_t mapping_size ) noexcept;

    static constexpr sizes_hdr unpack( header_info ) noexcept;

    [[ nodiscard, gnu::pure ]] size_type & stored_size() noexcept { return get_sizes().data_size; }

    void * expand_capacity( size_type target_storage_capacity );

    size_type client_to_storage_size( size_type sz ) const noexcept;

private:
    mapped_view view_;
    mapping     mapping_;
}; // contiguous_storage


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
// https://www.youtube.com/watch?v=SGdfPextuAU&t=3829s C++Now 2019: Arthur O'Dwyer “Trivially Relocatable” (persistability thoughts)
template <typename T>
bool constexpr does_not_hold_addresses{ std::is_fundamental_v<T> || std::is_enum_v<T> };


template <typename T, typename sz_t = std::size_t>
requires is_trivially_moveable<T>
class [[ clang::trivial_abi ]] vm_vector
    :
    public contiguous_storage,
    public vector_impl<vm_vector<T, sz_t>, T, sz_t>
{
    using storage_t = contiguous_storage;
    using vec_impl  = vector_impl<vm_vector<T, sz_t>, T, sz_t>;

public:
    using value_type = T;

    vm_vector(                    ) = default;
    vm_vector( vm_vector const &  ) = delete;
    vm_vector( vm_vector       && ) = default;
   ~vm_vector(                    ) = default;

    vm_vector & operator=( vm_vector &&      ) = default;
    vm_vector & operator=( vm_vector const & ) = default;

    auto map_file( auto const file, flags::named_object_construction_policy const policy, header_info const hdr_info = {} ) noexcept
    requires( does_not_hold_addresses<T> ) // is T safe to persist (or share in IPC)
    {
        static_assert( sizeof( *this ) == sizeof( contiguous_storage ) );
        return storage_t::map_file( file, policy, hdr_info.with_final_alignment_for<T>() );
    }

    template <typename InitPolicy = value_init_t>
    err::fallible_result<void, error>
    map_memory( sz_t const initial_data_size = 0, header_info const hdr_info = {}, InitPolicy = {} ) noexcept
    {
        auto result{ storage_t::map_memory(
            to_byte_sz( initial_data_size ),
            hdr_info.with_final_alignment_for<T>()
        ) };
        if
        (
            !initial_data_size ||
            !result ||
            std::is_same_v<InitPolicy, no_init_t> ||
            std::is_trivially_default_constructible_v<T>
        )
            return result.as_fallible_result();

        if constexpr ( std::is_same_v<InitPolicy, value_init_t> )
            std::ranges::uninitialized_value_construct  ( *this );
        else
            std::ranges::uninitialized_default_construct( *this );

        return err::success;
    }

    using storage_t::empty;
    using storage_t::swap;
    using vec_impl::grow_to;
    using vec_impl::shrink_to;
    using vec_impl::resize;

    [[ nodiscard, gnu::pure ]] T       * data()       noexcept { return to_t_ptr( storage_t::data() ); }
    [[ nodiscard, gnu::pure ]] T const * data() const noexcept { return const_cast<vm_vector &>( *this ).data(); }

    //! <b>Effects</b>: Returns the number of the elements contained in the vector.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard, gnu::pure ]] sz_t size() const noexcept { return to_t_sz( storage_t::size() ); }

    //! <b>Effects</b>: Number of elements for which memory has been allocated.
    //!   capacity() is always greater than or equal to size().
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard, gnu::pure ]] sz_t capacity() const noexcept{ return to_t_sz( storage_t::vm_capacity() ); }

    void reserve( sz_t const new_capacity ) { storage_t::reserve( to_byte_sz( new_capacity ) ); }

    // not really a standard allocator: providing a dummy alias simply to have boost::container::flat* compileable with this container.
    using allocator_type = std::allocator<T>;
    storage_t const & get_stored_allocator() const noexcept { return *this; }

    decltype( auto ) user_header_data() noexcept { return storage_t::header_storage(); }

    // helper getter for generic code that wishes to do basic manipulation on
    // vm::vectors w/o being templated (contiguous_storage does not publicize
    // functionality that could be used to make it out of sync with the
    // corresponding vm::vector)
    contiguous_storage       & storage_base()       noexcept { return *this; }
    contiguous_storage const & storage_base() const noexcept { return *this; }

private: friend vec_impl;
    //! <b>Effects</b>: If n is less than or equal to capacity(), this call has no
    //!   effect. Otherwise, it is a request for allocation of additional memory.
    //!   If the request is successful, then capacity() is greater than or equal to
    //!   n; otherwise, capacity() is unchanged. In either case, size() is unchanged.
    //!
    //! <b>Throws</b>: If memory allocation allocation throws or T's copy/move constructor throws.
    T * storage_grow_to  ( sz_t const target_size )          { return static_cast<T *>( storage_t::grow_to  ( to_byte_sz( target_size ) ) ); }
    T * storage_shrink_to( sz_t const target_size ) noexcept { return static_cast<T *>( storage_t::shrink_to( to_byte_sz( target_size ) ) ); }

    void storage_shrink_size_to( sz_t const new_size ) noexcept { storage_t::shrink_size_to( to_byte_sz( new_size ) ); }
    void storage_dec_size() noexcept { storage_shrink_size_to( size() - 1 ); }
    void storage_inc_size() noexcept; // TODO

    void storage_free() noexcept { storage_t::shrink_to( 0 ); }

private:
    PSI_WARNING_DISABLE_PUSH()
    PSI_WARNING_GCC_OR_CLANG_DISABLE( -Wsign-conversion )
    static T *  to_t_ptr  ( mapped_view::value_type * const ptr     ) noexcept {                                             return reinterpret_cast<T *>( ptr ); }
    static sz_t to_t_sz   ( auto                      const byte_sz ) noexcept { BOOST_ASSUME( byte_sz % sizeof( T ) == 0 ); return static_cast<sz_t>( byte_sz / sizeof( T ) ); }
    static sz_t to_byte_sz( auto                      const sz      ) noexcept
    {
        auto const rez{ sz * sizeof( T ) };
        BOOST_ASSERT( rez <= std::numeric_limits<sz_t>::max() );
        return static_cast<sz_t>( rez );
    }
    PSI_WARNING_DISABLE_POP()
}; // class vm_vector

PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
