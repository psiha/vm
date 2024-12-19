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

#include <boost/assert.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace detail
{
    template <typename T, bool> struct size           { T value; };
    template <typename T      > struct size<T, false> {};
} // namespace detail

class [[ clang::trivial_abi ]] contiguous_storage_base
{
public:
    // Mitigation for alignment codegen being sprayed allover at header_data
    // call sites - allow it to assume a small, 'good enough for most',
    // guaranteed alignment (event at the possible expense of slack space in
    // header hierarchies) so that alignment fixups can be skipped for such
    // headers.
    static std::uint8_t constexpr minimal_subheader_alignment{ alignof( int ) };
    static std::uint8_t constexpr minimal_total_header_size_alignment{ 16 };

    // TODO shallow/COW copy construction, + OSX vm_copy
    contiguous_storage_base( contiguous_storage_base && ) = default;
    contiguous_storage_base & operator=( contiguous_storage_base && ) = default;

    [[ nodiscard, gnu::pure, gnu::assume_aligned( reserve_granularity ) ]] auto * data()       noexcept { BOOST_ASSERT_MSG( mapping_, "Paging file not attached" ); return std::assume_aligned<commit_granularity>( view_.data() ); }
    [[ nodiscard, gnu::pure, gnu::assume_aligned( reserve_granularity ) ]] auto * data() const noexcept { return const_cast<contiguous_storage_base &>( *this ).data(); }

    //! <b>Effects</b>: Tries to deallocate the excess of memory created
    //!   with previous allocations. The size of the vector is unchanged
    //!
    //! <b>Throws</b>: nothing.
    //!
    //! <b>Complexity</b>: Constant.
    void shrink_to_fit() noexcept;

    void unmap() noexcept { view_.unmap(); }

    void close() noexcept;

    [[ nodiscard, gnu::pure ]] auto storage_size() const noexcept { return get_size( mapping_ ); }
    [[ nodiscard, gnu::pure ]] auto  mapped_size() const noexcept { return view_.size(); }

    void flush_async   () const noexcept { flush_async   ( 0, mapped_size() ); }
    void flush_blocking() const noexcept { flush_blocking( 0, mapped_size() ); }

    void flush_async   ( std::size_t beginning, std::size_t size ) const noexcept;
    void flush_blocking( std::size_t beginning, std::size_t size ) const noexcept;

    [[ nodiscard, gnu::pure ]] bool file_backed() const noexcept { return mapping_.is_file_based(); }

    [[ nodiscard, gnu::pure ]] bool has_attached_storage() const noexcept { return static_cast<bool>( mapping_ ); }

    explicit operator bool() const noexcept { return has_attached_storage(); }

    void swap( contiguous_storage_base & other ) noexcept { std::swap( *this, other ); }

protected:
    static bool constexpr storage_zero_initialized{ true };

    constexpr contiguous_storage_base() = default;

    err::fallible_result<std::size_t, error>
    map_file( auto const * const file_name, std::size_t const header_size, flags::named_object_construction_policy const policy ) noexcept
    {
        return map_file( create_file( file_name, create_rw_file_flags( policy ) ), header_size );
    }

    // template (char type) independent portion of map_file
    err::fallible_result<std::size_t, error>
    map_file( file_handle && file, std::size_t header_size ) noexcept;

    err::fallible_result<std::size_t, error>
    map_memory( std::size_t const size ) noexcept { return map( {}, size ); }

    void   reserve( std::size_t new_capacity );
    void * resize ( std::size_t target_size  );

    void * shrink_to( std::size_t target_size ) noexcept( mapping::views_downsizeable );
    void *   grow_to( std::size_t target_size );

    void * expand_view( std::size_t target_size );

    void shrink_mapped_size_to( std::size_t target_size ) noexcept( mapping::views_downsizeable );

    void free() noexcept;

private:
    err::fallible_result<std::size_t, error>
    map( file_handle && file, std::size_t mapping_size ) noexcept;

private:
    mapped_view view_;
    mapping     mapping_;
}; // contiguous_storage_base

template <typename sz_t, bool headerless>
class [[ clang::trivial_abi ]] contiguous_storage
    :
    public  contiguous_storage_base,
    private detail::size<sz_t, !headerless>
    // Checkout a revision prior to March the 21st 2024 for a version that used
    // statically sized header sizes. The current approach is more versatile
    // while the overhead should be less than negligible.
{
public:
    using size_type = sz_t;

private:
    static constexpr auto size_size{ headerless ? 0 : sizeof( size_type ) };
    using size_holder = detail::size<sz_t, !headerless>;

public:
             contiguous_storage(                             ) noexcept requires(  headerless ) = default;
    explicit contiguous_storage( size_type const header_size ) noexcept requires( !headerless ) : size_holder{ align_up( header_size, minimal_total_header_size_alignment ) } {}

    static constexpr std::uint8_t header_size() noexcept requires( headerless ) { return 0; }

    [[ gnu::pure ]] size_type header_size() const noexcept requires( !headerless )
    {
        auto const sz{ size_holder::value };
        BOOST_ASSUME( sz >= sizeof( sz_t ) );
        BOOST_ASSUME( is_aligned( sz, minimal_total_header_size_alignment ) );
        return sz;
    }

    using contiguous_storage_base::operator bool;

    [[ gnu::pure, nodiscard ]] auto header_storage()       noexcept { return std::span{ contiguous_storage_base::data(), header_size() - size_size }; }
    [[ gnu::pure, nodiscard ]] auto header_storage() const noexcept { return std::span{ contiguous_storage_base::data(), header_size() - size_size }; }

    err::fallible_result<void, error> map_file( auto const * const file_name, flags::named_object_construction_policy const policy ) noexcept
    {
        auto const sz{ contiguous_storage_base::map_file( file_name, header_size(), policy )() };
        if ( !sz )
            return sz.error();
        if constexpr ( !headerless )
        {
            auto & stored_size{ this->stored_size() };
            BOOST_ASSERT_MSG( stored_size <= *sz - header_size(), "Corrupted file: stored size larger than the file itself?" );
            // Clamp bogus/too large sizes (implicitly handles possible garbage on file creation).
            stored_size = std::min( stored_size, static_cast<size_type>( *sz - header_size() ) );
        }
        return err::success;
    }

    err::fallible_result<void, error> map_memory( size_type const size ) noexcept
    {
        auto const sz{ contiguous_storage_base::map_memory( size + header_size() )() };
        if ( !sz )
            return sz.error();
        BOOST_ASSERT( *sz == size + header_size() );
        if constexpr ( !headerless )
        {
            auto & stored_size{ this->stored_size() };
            BOOST_ASSUME( stored_size == 0 ); // Got garbage in an anonymous mapping!?
            stored_size = size;
        }
        return err::success;
    }

    auto data()       noexcept { return contiguous_storage_base::data() + header_size(); }
    auto data() const noexcept { return contiguous_storage_base::data() + header_size(); }

    //! <b>Effects</b>: Returns true if the vector contains no elements.
    //! <b>Throws</b>: Nothing.
    //! <b>Complexity</b>: Constant.
    [[ nodiscard, gnu::pure ]] bool empty() const noexcept { return !has_attached_storage() || !size(); }

    [[ nodiscard, gnu::pure ]] auto storage_size() const noexcept { return static_cast<size_type>( contiguous_storage_base::storage_size() ); }
    [[ nodiscard, gnu::pure ]] auto  mapped_size() const noexcept { return static_cast<size_type>( contiguous_storage_base:: mapped_size() ); }

    [[ nodiscard, gnu::pure ]] auto size    () const noexcept { if constexpr ( headerless ) return  mapped_size(); else return stored_size(); }
    [[ nodiscard, gnu::pure ]] auto capacity() const noexcept { if constexpr ( headerless ) return storage_size(); else return mapped_size() - header_size(); }

protected:
    void * grow_to( size_type const target_size )
    {
        BOOST_ASSUME( target_size >= size() );
        if constexpr ( headerless )
        {
            if ( target_size > size() )
                contiguous_storage_base::grow_to( target_size );
        }
        else
        {
            if ( target_size > capacity() )
                contiguous_storage_base::grow_to( target_size + header_size() );
            stored_size() = target_size;
        }
        return data();
    }

    void * shrink_to( size_type const target_size ) noexcept
    {
        auto const data_ptr{ contiguous_storage_base::shrink_to( target_size + header_size() ) };
        if constexpr ( !headerless )
            stored_size() = target_size;
        return data_ptr;
    }

    void resize( size_type const target_size )
    {
        if ( target_size > size() )
        { // partial replication of grow_to logic to avoid a double target_size > size() check
            if constexpr ( headerless )
                grow_to( target_size );
            else
            {
                if ( target_size > capacity() )
                    grow_to( target_size );
                else
                    stored_size() = target_size;
            }
        }
        else
        {
            // or skip this like std::vector and rely on an explicit shrink_to_fit() call?
            shrink_to( target_size );
        }
        if constexpr ( !headerless )
        {
            BOOST_ASSUME( stored_size() == target_size );
        }
    }

    void reserve( size_type const new_capacity )
    {
        if constexpr ( headerless )
            contiguous_storage_base::reserve( new_capacity );
        else
        if ( new_capacity > capacity() )
            contiguous_storage_base::grow_to( new_capacity + header_size() );
    }

    void shrink_to_fit() noexcept
    {
        if constexpr ( headerless )
            contiguous_storage_base::shrink_to_fit();
        else
            contiguous_storage_base::shrink_to( stored_size() + header_size() );
    }

    bool has_extra_capacity() const noexcept
    {
        BOOST_ASSERT( size() <= capacity() );
        return size() != capacity();
    }

    void grow_into_available_capacity_by( size_type const sz_delta ) noexcept
    {
        BOOST_ASSERT_MSG( sz_delta <= ( capacity() - size() ), "Out of preallocated space" );
        if constexpr ( headerless )
            contiguous_storage_base::expand_view( mapped_size() + sz_delta );
        else
            stored_size() += sz_delta;
    }

    void shrink_size_to( sz_t const new_size ) noexcept
    {
        BOOST_ASSUME( new_size <= capacity() );
        if constexpr ( headerless )
            contiguous_storage_base::shrink_mapped_size_to( new_size );
        else
            stored_size() = new_size;
    }

protected:
    void free() noexcept
    {
        if constexpr ( headerless )
            contiguous_storage_base::free();
        else
            shrink_to( 0 );
    }

    [[ nodiscard, gnu::pure ]] size_type & stored_size() noexcept requires( !headerless )
    {
        auto const p_size{ contiguous_storage_base::data() + header_size() - size_size };
        BOOST_ASSERT( reinterpret_cast<std::uintptr_t>( p_size ) % alignof( size_type ) == 0 );
        return *reinterpret_cast<size_type *>( p_size );
    }
    [[ nodiscard, gnu::pure ]] size_type   stored_size() const noexcept requires( !headerless )
    {
        return const_cast<contiguous_storage &>( *this ).stored_size();
    }
}; // contiguous_storage

template <typename T, typename sz_t, bool headerless>
class [[ clang::trivial_abi ]] typed_contiguous_storage
    :
    public contiguous_storage<sz_t, headerless>,
    public vector_impl<typed_contiguous_storage<T, sz_t, headerless>, T, sz_t>
{
private:
    using base     = contiguous_storage<sz_t, headerless>;
    using vec_impl = vector_impl<typed_contiguous_storage<T, sz_t, headerless>, T, sz_t>;

public:
    using value_type = T;

    using base::base;

    using base::empty;
    using vec_impl::grow_to;
    using vec_impl::shrink_to;
    using vec_impl::resize;

    [[ nodiscard, gnu::pure ]] T       * data()       noexcept { return to_t_ptr( base::data() ); }
    [[ nodiscard, gnu::pure ]] T const * data() const noexcept { return const_cast<typed_contiguous_storage &>( *this ).data(); }

    //! <b>Effects</b>: Returns the number of the elements contained in the vector.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard, gnu::pure ]] sz_t size() const noexcept { return to_t_sz( base::size() ); }

    //! <b>Effects</b>: Number of elements for which memory has been allocated.
    //!   capacity() is always greater than or equal to size().
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard, gnu::pure ]] sz_t capacity() const noexcept{ return to_t_sz( base::capacity() ); }

    void reserve( sz_t const new_capacity ) { base::reserve( to_byte_sz( new_capacity ) ); }

    decltype( auto ) user_header_data() noexcept { return base::header_storage(); }

    // helper getter for generic code that wishes to do basic manipulation on
    // vm::vectors w/o being templated (contiguous_storage_base does not
    // publicize functionality that could be used to make it out of sync with the
    // corresponding vm::vector)
    contiguous_storage_base & storage_base() noexcept { return *this; }

private: friend vec_impl;
    //! <b>Effects</b>: If n is less than or equal to capacity(), this call has no
    //!   effect. Otherwise, it is a request for allocation of additional memory.
    //!   If the request is successful, then capacity() is greater than or equal to
    //!   n; otherwise, capacity() is unchanged. In either case, size() is unchanged.
    //!
    //! <b>Throws</b>: If memory allocation allocation throws or T's copy/move constructor throws.
    T * storage_grow_to  ( sz_t const target_size )          { return static_cast<T *>( base::grow_to  ( to_byte_sz( target_size ) ) ); }
    T * storage_shrink_to( sz_t const target_size ) noexcept { return static_cast<T *>( base::shrink_to( to_byte_sz( target_size ) ) ); }

    void storage_shrink_size_to( sz_t const new_size ) noexcept { base::shrink_size_to( to_byte_sz( new_size ) ); }
    void storage_dec_size() noexcept { storage_shrink_size_to( size() - 1 ); }
    void storage_inc_size() noexcept; // TODO

    void storage_free() noexcept { base::free(); }

protected:
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
}; // class typed_contiguous_storage


struct header_info
{
    using align_t = std::uint16_t; // fit page_size

    static align_t constexpr minimal_subheader_alignment{ contiguous_storage_base::minimal_subheader_alignment };

    constexpr header_info() = default;
    constexpr header_info( std::uint32_t const size, align_t const alignment ) noexcept : header_size{ size }, data_extra_alignment{ std::max( alignment, minimal_subheader_alignment ) } {}
    template <typename T>
    constexpr header_info( std::in_place_type_t<T>, align_t const extra_alignment = alignof( T ) ) noexcept : header_info{ sizeof( T ), extra_alignment } {}
    template <typename T>
    static constexpr header_info make( align_t const extra_alignment = alignof( T ) ) noexcept { return { sizeof( T ), extra_alignment }; }

    template <typename AdditionalHeader>
    constexpr header_info add_header() const noexcept // support chained headers (class hierarchies)
    {
        auto const alignment{ std::max<align_t>( alignof( AdditionalHeader ), minimal_subheader_alignment ) };
        auto const padded_size{ align_up( this->header_size, alignment ) };
        return
        {
            static_cast<std::uint32_t>( padded_size + sizeof( AdditionalHeader ) ),
            std::max( final_alignment(), alignment )
        };
    }

    template <typename ... T>
    constexpr header_info with_alignment_for() const noexcept
    {
        return { this->header_size, std::max<align_t>( { this->final_alignment(), alignof( T )... } ) };
    }

    constexpr std::uint32_t final_header_size() const noexcept { return align_up( header_size, final_alignment() ); }
    constexpr align_t       final_alignment  () const noexcept
    {
        BOOST_ASSUME( data_extra_alignment >= minimal_subheader_alignment );
        auto const is_pow2{ std::has_single_bit( data_extra_alignment ) };
        BOOST_ASSUME( is_pow2 );
        return data_extra_alignment;
    }

    std::uint32_t header_size         { 0 };
    align_t       data_extra_alignment{ minimal_subheader_alignment }; // e.g. for vectorization or overlaying complex types over std::byte storage
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
            hdr_storage.subspan( align_up<in_alignment>( sizeof( Header ) ) )
        };
    }
    else
    {
        auto const     raw_data { std::assume_aligned<in_alignment>( hdr_storage.data() ) };
        auto const aligned_data { align_up<alignof( Header )>( raw_data ) };
        auto const aligned_space{ static_cast<std::uint32_t>( hdr_storage.size() ) - unsigned( aligned_data - raw_data ) };
        BOOST_ASSUME( aligned_space >= sizeof( Header ) );
        return std::pair
        {
            reinterpret_cast<Header *>( aligned_data ),
            std::span{ align_up<in_alignment>( aligned_data + sizeof( Header ) ), aligned_space - sizeof( Header ) }
        };
    }
}

// Standard-vector-like/compatible _presistent_ container class template
// which uses VM/a mapped object for its backing storage. Currently limited
// to trivial_abi types.
// Used the Boost.Container vector as a starting skeleton.

template <typename T, typename sz_t = std::size_t, bool headerless_param = true>
requires is_trivially_moveable<T>
class [[ clang::trivial_abi ]] vm_vector
    :
    public typed_contiguous_storage<T, sz_t, headerless_param>
{
private:
    using storage_t = typed_contiguous_storage<T, sz_t, headerless_param>;
    using impl      = storage_t;

public:
    static constexpr auto headerless{ headerless_param };

    // not really a standard allocator: providing the alias simply to have boost::container::flat* compileable with this container.
    using allocator_type = std::allocator<T>;

    vm_vector() noexcept requires( headerless ) {}
    // Allowing for a header to store/persist the 'actual' size of the container can be generalized
    // to storing arbitrary (types of) headers. This however makes this class template no longer
    // model just a vector like container but rather a structure consisting of a fixed-sized part
    // (the header) and a dynamically resizable part (the vector part) - kind of like the 'curiously
    // recurring C pattern' of a struct with a zero-sized trailing array data member.
    // Awaiting a better name for the idiom, considering its usefulness, the library exposes this
    // ability/functionality publicly - as a runtime parameter (instead of a Header template type
    // parameter) - the relative runtime cost should be near non-existent vs all the standard
    // benefits (having concrete base classes, less template instantiations and codegen copies...).
    explicit vm_vector( header_info const hdr_info = {} ) noexcept requires( !headerless )
        // TODO: use slack space (if any) in the object placed (by user code) in the header space
        // to store the size (to avoid wasting even more slack space due to alignment padding
        // after appending the size ('member').
        :
        storage_t
        (
            hdr_info
                .         add_header<sz_t>()
                .template with_alignment_for<sz_t, T>()
                .         final_header_size()
        ) {}

    vm_vector( vm_vector const &  ) = delete;
    vm_vector( vm_vector       && ) = default;

   ~vm_vector() noexcept = default;

    vm_vector & operator=( vm_vector &&      ) = default;
    vm_vector & operator=( vm_vector const & ) = default;

    auto map_file  ( auto const file, flags::named_object_construction_policy const policy ) noexcept { BOOST_ASSUME( !storage_t::has_attached_storage() ); return storage_t::map_file( file, policy ); }
    template <typename InitPolicy = value_init_t>
    auto map_memory( sz_t const initial_size = 0, InitPolicy init_policy = {} ) noexcept
    {
        BOOST_ASSUME( !storage_t::has_attached_storage() );
        auto result{ storage_t::map_memory( storage_t::to_byte_sz( initial_size ) )() };
        if ( std::is_same_v<decltype( init_policy ), value_init_t> && initial_size && result ) {
            std::uninitialized_default_construct( impl::begin(), impl::end() );
        }
        return result.as_fallible_result();
    }

    //! <b>Effects</b>: Returns a copy of the internal allocator.
    //!
    //! <b>Throws</b>: If allocator's copy constructor throws.
    //!
    //! <b>Complexity</b>: Constant.
    storage_t const & get_stored_allocator() const noexcept { return *this; }
}; // class vector

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
