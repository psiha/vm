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

#include "align.hpp"
#include "mapping/mapping.hpp"
#include "mapped_view/mapped_view.hpp"
#include "mapped_view/ops.hpp"
#include "mappable_objects/file/file.hpp"
#include "mappable_objects/file/utility.hpp"

#include <psi/build/disable_warnings.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <climits>
#include <memory>
#include <span>
#include <type_traits>
#include <stdexcept>
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------

namespace detail
{
    [[ noreturn ]] inline void throw_out_of_range() { throw std::out_of_range( "vm::vector access out of bounds" ); }
} // namespace detail

class contiguous_container_storage_base
{
public:
    contiguous_container_storage_base( contiguous_container_storage_base && ) = default;
    contiguous_container_storage_base & operator=( contiguous_container_storage_base && ) = default;

    auto data()       noexcept { BOOST_ASSERT_MSG( mapping_, "Paging file not attached" ); return std::assume_aligned<commit_granularity>( view_.data() ); }
    auto data() const noexcept { return const_cast< contiguous_container_storage_base & >( *this ).data(); }

    void unmap() noexcept { view_.unmap(); }

    void close() noexcept
    {
        mapping_.close();
        unmap();
    }

    [[ gnu::pure ]] auto storage_size() const noexcept { return get_size( mapping_ ); }
    [[ gnu::pure ]] auto  mapped_size() const noexcept { return view_.size(); }

    void flush_async   ( std::size_t const beginning, std::size_t const size ) noexcept { vm::flush_async   ( mapped_span({ view_.subspan( beginning, size ) }) ); }
    void flush_blocking( std::size_t const beginning, std::size_t const size ) noexcept { vm::flush_blocking( mapped_span({ view_.subspan( beginning, size ) }), mapping_.underlying_file() ); }

    bool file_backed() const noexcept { return mapping_.get() == handle::invalid_value; }

    explicit operator bool() const noexcept { return static_cast<bool>( mapping_ ); }

protected:
    constexpr contiguous_container_storage_base() = default;

    err::fallible_result<std::size_t, error>
    map_file( auto const * const file_name, std::size_t const header_size, flags::named_object_construction_policy const policy ) noexcept
    {
        return map_file( create_file( file_name, create_rw_file_flags( policy ) ), header_size );
    }

    err::fallible_result<std::size_t, error>
    map_memory( std::size_t const size ) noexcept { return map( {}, size ); }


    void expand( std::size_t const target_size )
    {
        set_size( mapping_, target_size );
        expand_view( target_size );
    }

    void expand_view( std::size_t const target_size )
    {
        BOOST_ASSERT( get_size( mapping_ ) >= target_size );
        view_.expand( target_size, mapping_ );
    }

    void shrink( std::size_t const target_size ) noexcept
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
    }

    void shrink_to_fit() noexcept { set_size( mapping_, mapped_size() )().assume_succeeded(); }

    void resize( std::size_t const target_size )
    {
        if ( target_size > mapped_size() ) expand( target_size );
        else                               shrink( target_size );
    }

    void reserve( std::size_t const new_capacity )
    {
        if ( new_capacity > storage_size() )
            set_size( mapping_, new_capacity );
    }

    // template (char type) independent portion of map_file
    err::fallible_result<std::size_t, error>
    map_file( file_handle && file, std::size_t const header_size ) noexcept
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

private:
    err::fallible_result<std::size_t, error>
    map( file_handle && file, std::size_t const mapping_size ) noexcept
    {
        using ap    = flags::access_privileges;
        using flags = flags::mapping;
        mapping_ = create_mapping
        (
            std::move( file ),
            ap::object{ ap::readwrite },
            ap::child_process::does_not_inherit,
            flags::share_mode::shared,
            mapping::supports_zero_sized_mappings
                ? mapping_size
                : std::max( std::size_t{ 1 }, mapping_size )
        );
        if ( !mapping_ )
            return error{};

        view_ = mapped_view::map( mapping_, 0, mapping_size );
        if ( !view_.data() && ( mapping::supports_zero_sized_views || mapping_size != 0 ) )
            return error{};

        return std::size_t{ mapping_size };
    }

private:
    mapped_view view_;
    mapping     mapping_;
}; // contiguous_container_storage_base

template < typename sz_t, bool headerless >
class contiguous_container_storage
    :
    public  contiguous_container_storage_base,
    private std::conditional_t<headerless, std::false_type, std::tuple<sz_t>>
    // checkout a revision prior to March the 21st 2024 for a version that used statically sized header sizes
    // this approach is more versatile while the overhead should be less than negligible
{
public:
    using size_type = sz_t;

private:
    static constexpr auto size_size{ headerless ? 0 : sizeof( size_type ) };

public:
             contiguous_container_storage(                             ) noexcept requires(  headerless ) = default;
    explicit contiguous_container_storage( size_type const header_size ) noexcept requires( !headerless ) : std::tuple<sz_t>{ header_size } {}

    static constexpr std::uint8_t header_size() noexcept requires( headerless ) { return 0; }

    [[ gnu::pure ]] size_type header_size() const noexcept requires( !headerless )
    {
        auto const sz{ std::get<0>( *this ) };
        BOOST_ASSUME( sz >= sizeof( sz_t ) );
        return sz;
    }

    using contiguous_container_storage_base::operator bool;

    [[ gnu::pure, nodiscard ]] auto header_storage()       noexcept { return std::span{ contiguous_container_storage_base::data(), header_size() - size_size }; }
    [[ gnu::pure, nodiscard ]] auto header_storage() const noexcept { return std::span{ contiguous_container_storage_base::data(), header_size() - size_size }; }

    err::fallible_result<void, error> map_file( auto const * const file_name, flags::named_object_construction_policy const policy ) noexcept
    {
        auto const sz{ contiguous_container_storage_base::map_file( file_name, header_size(), policy )() };
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
        auto const sz{ contiguous_container_storage_base::map_memory( size + header_size() )() };
        if ( !sz )
            return sz.error();
        if constexpr ( !headerless )
        {
            auto & stored_size{ this->stored_size() };
            BOOST_ASSERT_MSG( stored_size == 0, "Got garbage in an anonymous mapping!?" );
            stored_size = size;
        }
        return err::success;
    }

    auto data()       noexcept { return contiguous_container_storage_base::data() + header_size(); }
    auto data() const noexcept { return contiguous_container_storage_base::data() + header_size(); }

    [[ gnu::pure, nodiscard ]] auto storage_size() const noexcept { return static_cast<size_type>( contiguous_container_storage_base::storage_size() ); }
    [[ gnu::pure, nodiscard ]] auto  mapped_size() const noexcept { return static_cast<size_type>( contiguous_container_storage_base:: mapped_size() ); }

    [[ gnu::pure, nodiscard ]] auto size    () const noexcept { if constexpr ( headerless ) return  mapped_size(); else return stored_size(); }
    [[ gnu::pure, nodiscard ]] auto capacity() const noexcept { if constexpr ( headerless ) return storage_size(); else return mapped_size() - header_size(); }

    void expand( size_type const target_size )
    {
        BOOST_ASSUME( target_size >= size() );
        if constexpr ( headerless )
        {
            if ( target_size > size() )
                contiguous_container_storage_base::expand( target_size );
        }
        else
        {
            if ( target_size > capacity() )
                contiguous_container_storage_base::expand( target_size + header_size() );
            stored_size() = target_size;
        }
    }

    void shrink( size_type const target_size ) noexcept
    {
        contiguous_container_storage_base::shrink( target_size + header_size() );
        if constexpr ( !headerless )
            stored_size() = target_size;
    }

    void resize( size_type const target_size )
    {
        if ( target_size > size() )
        {
            if constexpr ( headerless )
                expand( target_size );
            else
            {
                if ( target_size > capacity() )
                    expand( target_size );
                else
                    stored_size() = target_size;
            }
        }
        else
        {
            // or skip this like std::vector and rely on an explicit shrink_to_fit() call?
            shrink( target_size );
        }
        if constexpr ( !headerless )
            { BOOST_ASSUME( stored_size() == target_size ); }
    }

    void reserve( size_type const new_capacity )
    {
        if constexpr ( headerless )
            contiguous_container_storage_base::reserve( new_capacity );
        else
        if ( new_capacity > capacity() )
            contiguous_container_storage_base::expand( new_capacity + header_size() );
    }

    void shrink_to_fit() noexcept
    {
        if constexpr ( headerless )
            contiguous_container_storage_base::shrink_to_fit();
        else {
            contiguous_container_storage_base::shrink( stored_size() + header_size() );
        }
    }

    bool has_extra_capacity() const noexcept
    {
        BOOST_ASSERT( size() <= capacity() );
        return size() != capacity();
    }

    void allocate_available_capacity( size_type const sz ) noexcept
    {
        BOOST_ASSERT_MSG( sz <= ( capacity() - size() ), "Out of preallocated space" );
        if constexpr ( headerless )
            contiguous_container_storage_base::expand_view( mapped_size() + sz );
        else
            stored_size() += sz;
    }

private:
    [[ gnu::pure, nodiscard ]] size_type & stored_size() noexcept requires( !headerless )
    {
        auto const p_size{ contiguous_container_storage_base::data() + header_size() - size_size };
        BOOST_ASSERT( reinterpret_cast<std::intptr_t>( p_size ) % alignof( size_type ) == 0 );
        return *reinterpret_cast<size_type *>( p_size );
    }
    [[ gnu::pure, nodiscard ]] size_type   stored_size() const noexcept requires( !headerless )
    {
        return const_cast<contiguous_container_storage &>( *this ).stored_size();
    }
}; // contiguous_container_storage

template < typename T >
bool constexpr is_trivially_moveable
{
#ifdef __clang__
    __is_trivially_relocatable( T ) ||
#endif
    std::is_trivially_move_constructible_v< T >
};

struct default_init_t{}; inline constexpr default_init_t default_init;
struct value_init_t  {}; inline constexpr value_init_t   value_init  ;

// Standard-vector-like/compatible _presistent_ container class template
// which uses VM/a mapped object for its backing storage. Currently limited
// to trivial_abi types.
// Used the Boost.Container vector as a starting skeleton.

template < typename T, typename sz_t = std::size_t, bool headerless_param = true >
requires is_trivially_moveable< T >
class vector
{
private:
    using storage_t = contiguous_container_storage< sz_t, headerless_param >;
    storage_t storage_;

public:
    static constexpr auto headerless{ headerless_param };

    using       span_t = std::span<T      >;
    using const_span_t = std::span<T const>;

    using value_type             = T;
    using       pointer          = T       *;
    using const_pointer          = T const *;
    using       reference        = T       &;
    using const_reference        = T const &;
    using param_const_ref        = std::conditional_t< std::is_trivial_v< T > && ( sizeof( T ) <= 2 * sizeof( void * ) ), T, const_reference >;
    using       size_type        = sz_t;
    using difference_type        = std::make_signed_t<size_type>;
    using         iterator       = typename span_t::      iterator;
    using reverse_iterator       = typename span_t::      reverse_iterator;
#if defined( _LIBCPP_VERSION ) // no span::const_iterators yet (as of v17)
    using const_iterator         = typename const_span_t::iterator;
    using const_reverse_iterator = typename const_span_t::reverse_iterator;
#else
    using const_iterator         = typename span_t::const_iterator;
    using const_reverse_iterator = typename span_t::const_reverse_iterator;
#endif

    // not really a standard allocator: providing the alias simply to have boost::container::flat* compileable with this container.
    using allocator_type = std::allocator<value_type>;

public:
    vector() noexcept requires( headerless ) {}
    // Allowing for a header to store/persist the 'actual' size of the container can be generalized
    // to storing arbitrary (types of) headers. This however makes this class template no longer
    // model just a vector like container but rather a structure consisting of a fixed-sized part
    // (the header) and a dynamically resizable part (the vector part) - kind of like the 'curiously
    // recurring C pattern' of a struct with a zero-sized trailing array data member.
    // Awaiting a better name for the idiom, considering its usefulness, the library exposes this
    // ability/functionality publicly - as a runtime parameter (instead of a Header template type
    // parameter) - the relative runtime cost should be near non-existent vs all the standard
    // benefits (having concrete base clasess, less template instantiations and codegen copies...).
    explicit vector
    (
        size_type const header_size      = 0,
        size_type const header_alignment = 0
    ) noexcept requires( !headerless )
        // TODO: use slack space (if any) in the object placed (by user code) in the header space
        // to store the size (to avoid wasting even more slack space due to alignment padding
        // after appending the size ('member').
        :
        storage_
        (
            static_cast<size_type>( align_up
            (
                header_size + sizeof( size_type ),
                std::max<size_type>({ alignof( size_type ), alignof( T ), header_alignment })
            ))
        ) {}

    vector( vector const & ) = delete;
    vector( vector && ) = default;

    ~vector() noexcept = default;

    vector & operator=( vector && ) = default;
    vector & operator=( vector const & x )
    {
        BOOST_ASSUME( &x != this ); // not going to support self assignment
        assign( x );
        return *this;
    }

   vector & operator=( std::initializer_list<value_type> const data ) { this->assign( data ); return *this; }

    //! <b>Effects</b>: Assigns the the range [first, last) to *this.
    //!
    //! <b>Throws</b>: If memory allocation throws or T's copy/move constructor/assignment or
    //!   T's constructor/assignment from dereferencing InpIt throws.
    //!
    //! <b>Complexity</b>: Linear to n.
    template < std::input_iterator It>
    void assign( It first, It const last )
    {
        //Overwrite all elements we can from [first, last)
        auto       cur   { this->begin() };
        auto const end_it{ this->end  () };
        for ( ; first != last && cur != end_it; ++cur, ++first )
            *cur = *first;

        if ( first == last )
        {
            auto const target_size{ static_cast<size_type>( cur - begin() ) };
            //There are no more elements in the sequence, erase remaining
            while ( cur != end_it )
                (*cur++).~value_type();
            shrink_storage_to( target_size );
        }
        else
        {
            //There are more elements in the range, insert the remaining ones
            this->append_range( std::span{ first, last } );
        }
    }

    void assign( std::initializer_list<T> const data ) { this->assign( data.begin(), data.end() ); }

    //! <b>Effects</b>: Assigns the n copies of val to *this.
    //!
    //! <b>Throws</b>: If memory allocation throws or
    //!   T's copy/move constructor/assignment throws.
    //!
    //! <b>Complexity</b>: Linear to n.
    void assign( size_type const n, param_const_ref val ) = /*TODO*/delete;

    //! <b>Effects</b>: Returns a copy of the internal allocator.
    //!
    //! <b>Throws</b>: If allocator's copy constructor throws.
    //!
    //! <b>Complexity</b>: Constant.
    auto const & get_stored_allocator() const noexcept { return storage_; }

    //////////////////////////////////////////////
    //
    //                iterators
    //
    //////////////////////////////////////////////

    //! <b>Effects</b>: Returns an iterator to the first element contained in the vector.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] auto  begin()       noexcept { return span().begin(); }
    [[ nodiscard ]] auto  begin() const noexcept { return cbegin(); }
    [[ nodiscard ]] auto cbegin() const noexcept
#if defined( _MSC_VER )
    { return const_cast<vector &>( *this ).span().cbegin(); } // wrkrnd for span<T> and span<T const> iterators not being compatible w/ MS STL
#else
    { return span().begin(); }
#endif

    //! <b>Effects</b>: Returns an iterator to the end of the vector.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] auto  end()       noexcept { return span().end(); }
    [[ nodiscard ]] auto  end() const noexcept { return cend(); }
    [[ nodiscard ]] auto cend() const noexcept
#if defined( _MSC_VER )
    { return const_cast<vector &>( *this ).span().cend(); }
#else
    { return span().end(); }
#endif

    //! <b>Effects</b>: Returns a reverse_iterator pointing to the beginning
    //! of the reversed vector.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] auto  rbegin()       noexcept { return span().rbegin(); }
    [[ nodiscard ]] auto  rbegin() const noexcept { return span().rbegin(); }
    [[ nodiscard ]] auto crbegin() const noexcept { return        rbegin(); }

    //! <b>Effects</b>: Returns a reverse_iterator pointing to the end
    //! of the reversed vector.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] auto  rend()       noexcept { return span().rend(); }
    [[ nodiscard ]] auto  rend() const noexcept { return static_cast<span_t const &>( const_cast<vector &>( *this ).span() ).rend(); }
    [[ nodiscard ]] auto crend() const noexcept { return        rend(); }

    //////////////////////////////////////////////
    //
    //                capacity
    //
    //////////////////////////////////////////////

    //! <b>Effects</b>: Returns true if the vector contains no elements.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard, gnu::pure ]] bool empty() const noexcept { return span().empty(); }

    //! <b>Effects</b>: Returns the number of the elements contained in the vector.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard, gnu::pure ]] size_type size() const noexcept { return to_t_sz( storage_.size() ); }

    //! <b>Effects</b>: Returns the largest possible size of the vector.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] static constexpr size_type max_size() noexcept { return static_cast< size_type >( std::numeric_limits< size_type >::max() / sizeof( value_type ) ); }

    //! <b>Effects</b>: Inserts or erases elements at the end such that
    //!   the size becomes n. New elements can be default or value initialized.
    //!
    //! <b>Throws</b>: If memory allocation throws, or T's copy/move or value initialization throws.
    //!
    //! <b>Complexity</b>: Linear to the difference between size() and new_size.
    void resize( size_type const new_size, default_init_t ) requires( std::is_trivial_v< T > )
    {
        storage_.resize( to_byte_sz( new_size ) );
    }
    void resize( size_type const new_size, value_init_t )
    {
        if ( new_size > size() )   grow_to( new_size, value_init );
        else                     shrink_to( new_size             );
    }

    //! <b>Effects</b>: Inserts or erases elements at the end such that
    //!   the size becomes n. New elements are copy constructed from x.
    //!
    //! <b>Throws</b>: If memory allocation throws, or T's copy/move constructor throws.
    //!
    //! <b>Complexity</b>: Linear to the difference between size() and new_size.
    void resize( size_type const new_size, param_const_ref x )
    {
        auto const current_size{ size() };
        BOOST_ASSUME( new_size >= current_size );
        storage_.resize( to_byte_sz( new_size ) );
        auto uninitialized_span{ span().subspan( current_size ) };
        std::uninitialized_fill( uninitialized_span.begin(), uninitialized_span.end(), x );
    }

    //! <b>Effects</b>: Number of elements for which memory has been allocated.
    //!   capacity() is always greater than or equal to size().
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] size_type capacity() const noexcept{ return to_t_sz( storage_.capacity() ); }

    //! <b>Effects</b>: If n is less than or equal to capacity(), this call has no
    //!   effect. Otherwise, it is a request for allocation of additional memory.
    //!   If the request is successful, then capacity() is greater than or equal to
    //!   n; otherwise, capacity() is unchanged. In either case, size() is unchanged.
    //!
    //! <b>Throws</b>: If memory allocation allocation throws or T's copy/move constructor throws.
    void reserve( size_type const new_capacity ) { storage_.reserve( to_byte_sz( new_capacity ) ); }

    //! <b>Effects</b>: Tries to deallocate the excess of memory created
    //!   with previous allocations. The size of the vector is unchanged
    //!
    //! <b>Throws</b>: If memory allocation throws, or T's copy/move constructor throws.
    //!
    //! <b>Complexity</b>: Linear to size().
    void shrink_to_fit() { storage_.shrink_to_fit(); }

    //////////////////////////////////////////////
    //
    //               element access
    //
    //////////////////////////////////////////////

    //! <b>Requires</b>: !empty()
    //!
    //! <b>Effects</b>: Returns a reference to the first
    //!   element of the container.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] reference front() noexcept { return span().front(); }

    //! <b>Requires</b>: !empty()
    //!
    //! <b>Effects</b>: Returns a const reference to the first
    //!   element of the container.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] const_reference front() const noexcept { return span().front(); }

    //! <b>Requires</b>: !empty()
    //!
    //! <b>Effects</b>: Returns a reference to the last
    //!   element of the container.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] reference back() noexcept { return span().back(); }

    //! <b>Requires</b>: !empty()
    //!
    //! <b>Effects</b>: Returns a const reference to the last
    //!   element of the container.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] const_reference back() const noexcept { return span().back(); }

    //! <b>Requires</b>: size() > n.
    //!
    //! <b>Effects</b>: Returns a reference to the nth element
    //!   from the beginning of the container.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] reference operator[]( size_type const n ) noexcept { return span()[ n ]; }

    //! <b>Requires</b>: size() > n.
    //!
    //! <b>Effects</b>: Returns a const reference to the nth element
    //!   from the beginning of the container.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] const_reference operator[]( size_type const n ) const noexcept { return span()[ n ]; }

    //! <b>Requires</b>: size() >= n.
    //!
    //! <b>Effects</b>: Returns an iterator to the nth element
    //!   from the beginning of the container. Returns end()
    //!   if n == size().
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    //!
    //! <b>Note</b>: Non-standard extension
    [[ nodiscard ]] iterator nth( size_type const n ) noexcept
    {
        BOOST_ASSERT( n <= size() );
        return begin() + static_cast<difference_type>( n );
    }

    //! <b>Requires</b>: size() >= n.
    //!
    //! <b>Effects</b>: Returns a const_iterator to the nth element
    //!   from the beginning of the container. Returns end()
    //!   if n == size().
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    //!
    //! <b>Note</b>: Non-standard extension
    [[ nodiscard ]] const_iterator nth( size_type const n ) const noexcept
    {
        BOOST_ASSERT( n <= size() );
        return begin() + static_cast<difference_type>( n );
    }

    //! <b>Requires</b>: begin() <= p <= end().
    //!
    //! <b>Effects</b>: Returns the index of the element pointed by p
    //!   and size() if p == end().
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    //!
    //! <b>Note</b>: Non-standard extension
    [[ nodiscard ]] size_type index_of( iterator const p ) noexcept
    {
        verify_iterator( p );
        return static_cast<size_type>( p - begin() );
    }

    //! <b>Requires</b>: begin() <= p <= end().
    //!
    //! <b>Effects</b>: Returns the index of the element pointed by p
    //!   and size() if p == end().
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    //!
    //! <b>Note</b>: Non-standard extension
    [[ nodiscard ]] size_type index_of( const_iterator const p ) const noexcept
    {
        verify_iterator( p );
        return static_cast<size_type>( p - begin() );
    }

    //! <b>Requires</b>: size() > n.
    //!
    //! <b>Effects</b>: Returns a reference to the nth element
    //!   from the beginning of the container.
    //!
    //! <b>Throws</b>: range_error if n >= size()
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] reference at( size_type const n )
    {
        if constexpr ( requires{ span().at( n ); } ) // cpp23
            return span().at( n );
        else
        {
            if ( n >= size() )
                detail::throw_out_of_range();
            return (*this)[ n ];
        }
    }

    //! <b>Requires</b>: size() > n.
    //!
    //! <b>Effects</b>: Returns a const reference to the nth element
    //!   from the beginning of the container.
    //!
    //! <b>Throws</b>: range_error if n >= size()
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] const_reference at( size_type const n ) const
    {
        if constexpr ( requires{ span().at( n ); } ) // cpp23
            return span().at( n );
        else
        {
            if ( n >= size() )
                detail::throw_out_of_range();
            return (*this)[ n ];
        }
    }

    //////////////////////////////////////////////
    //
    //                 data access
    //
    //////////////////////////////////////////////

    //! <b>Returns</b>: A pointer such that [data(),data() + size()) is a valid range.
    //!   For a non-empty vector, data() == &front().
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] T       * data()       noexcept { return to_t_ptr( storage_.data() ); }
    [[ nodiscard ]] T const * data() const noexcept { return const_cast< vector & >( *this ).data(); }

    [[ nodiscard ]]       span_t span()       noexcept { return { data(), size() }; }
    [[ nodiscard ]] const_span_t span() const noexcept { return { data(), size() }; }

    //////////////////////////////////////////////
    //
    //                modifiers
    //
    //////////////////////////////////////////////

    //! <b>Effects</b>: Inserts an object of type T constructed with
    //!   std::forward<Args>(args)... in the end of the vector.
    //!
    //! <b>Returns</b>: A reference to the created object.
    //!
    //! <b>Throws</b>: If memory allocation throws or the in-place constructor throws or
    //!   T's copy/move constructor throws.
    //!
    //! <b>Complexity</b>: Amortized constant time.
    template <class ...Args>
    reference emplace_back( Args &&...args )
    {
        storage_.expand( to_byte_sz( size() + 1 ) );
        if constexpr ( sizeof...( args ) )
            return *std::construct_at( &back(), std::forward< Args >( args )... );
        else
            return *new ( &back() ) T; // default init
    }

   //! <b>Effects</b>: Inserts an object of type T constructed with
   //!   std::forward<Args>(args)... in the end of the vector.
   //!
   //! <b>Throws</b>: If the in-place constructor throws.
   //!
   //! <b>Complexity</b>: Constant time.
   //!
   //! <b>Note</b>: Non-standard extension.
   template<class ...Args>
   bool stable_emplace_back(Args &&...args) = /*TODO*/delete;

    //! <b>Requires</b>: position must be a valid iterator of *this.
    //!
    //! <b>Effects</b>: Inserts an object of type T constructed with
    //!   std::forward<Args>(args)... before position
    //!
    //! <b>Throws</b>: If memory allocation throws or the in-place constructor throws or
    //!   T's copy/move constructor/assignment throws.
    //!
    //! <b>Complexity</b>: If position is end(), amortized constant time
    //!   Linear time otherwise.
    template<class ...Args>
    iterator emplace( const_iterator const position, Args && ...args )
    {
        auto const iter{ make_space_for_insert( position, 1 ) };
        std::construct_at( &*iter, std::forward< Args >( args )... );
        return iter;
    }

    //! <b>Effects</b>: Inserts a copy of x at the end of the vector.
    //!
    //! <b>Throws</b>: If memory allocation throws or
    //!   T's copy/move constructor throws.
    //!
    //! <b>Complexity</b>: Amortized constant time.
    void push_back( param_const_ref x ) { emplace_back( x ); }

    //! <b>Effects</b>: Constructs a new element in the end of the vector
    //!   and moves the resources of x to this new element.
    //!
    //! <b>Throws</b>: If memory allocation throws or
    //!   T's copy/move constructor throws.
    //!
    //! <b>Complexity</b>: Amortized constant time.
    void push_back( T && x ) requires( !std::is_trivial_v< T > ) { emplace_back( std::move( x ) ); }

    //! <b>Requires</b>: position must be a valid iterator of *this.
    //!
    //! <b>Effects</b>: Insert a copy of x before position.
    //!
    //! <b>Throws</b>: If memory allocation throws or T's copy/move constructor/assignment throws.
    //!
    //! <b>Complexity</b>: If position is end(), amortized constant time
    //!   Linear time otherwise.
    iterator insert( const_iterator const position, param_const_ref x ) { return emplace( position, x ); }

    //! <b>Requires</b>: position must be a valid iterator of *this.
    //!
    //! <b>Effects</b>: Insert a new element before position with x's resources.
    //!
    //! <b>Throws</b>: If memory allocation throws.
    //!
    //! <b>Complexity</b>: If position is end(), amortized constant time
    //!   Linear time otherwise.
    iterator insert( const_iterator const position, T && x ) requires( !std::is_trivial_v< T > ) { return emplace( position, std::move( x ) ); }

    //! <b>Requires</b>: position must be a valid iterator of *this.
    //!
    //! <b>Effects</b>: Insert n copies of x before pos.
    //!
    //! <b>Returns</b>: an iterator to the first inserted element or p if n is 0.
    //!
    //! <b>Throws</b>: If memory allocation throws or T's copy/move constructor throws.
    //!
    //! <b>Complexity</b>: Linear to n.
    iterator insert( const_iterator const position, size_type const n, param_const_ref x )
    {
        auto const iter{ make_space_for_insert( position, n ) };
        std::fill_n( iter, n, x );
        return iter;
    }

    //! <b>Requires</b>: position must be a valid iterator of *this.
    //!
    //! <b>Effects</b>: Insert a copy of the [first, last) range before pos.
    //!
    //! <b>Returns</b>: an iterator to the first inserted element or pos if first == last.
    //!
    //! <b>Throws</b>: If memory allocation throws, T's constructor from a
    //!   dereferenced InpIt throws or T's copy/move constructor/assignment throws.
    //!
    //! <b>Complexity</b>: Linear to boost::container::iterator_distance [first, last).
    template <std::input_iterator InIt>
    iterator insert( const_iterator const position, InIt const first, InIt const last )
    {
        auto const n{ static_cast<size_type>( std::distance( first, last ) ) };
        auto const iter{ make_space_for_insert( position, n ) };
        std::copy_n( first, n, iter );
        return iter;
    }

    //! <b>Requires</b>: position must be a valid iterator of *this.
    //!
    //! <b>Effects</b>: Insert a copy of the [il.begin(), il.end()) range before position.
    //!
    //! <b>Returns</b>: an iterator to the first inserted element or position if first == last.
    //!
    //! <b>Complexity</b>: Linear to the range [il.begin(), il.end()).
    iterator insert( const_iterator const position, std::initializer_list<value_type> const il )
    {
        return insert( position, il.begin(), il.end() );
    }

    void append_range( std::ranges::range auto && __restrict rng )
    {
        auto const current_size{ size() };
        storage_.expand( to_byte_sz( current_size + std::size( rng ) ) );
        std::uninitialized_copy( std::begin( rng ), std::end( rng ), nth( current_size ) );
    }
    void append_range( std::initializer_list< value_type > const rng ) { append_range( std::span{ rng.begin(), rng.end() } ); }

    //! <b>Effects</b>: Removes the last element from the container.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant time.
    void pop_back() noexcept
    {
        BOOST_ASSERT(!this->empty());
        std::destroy_at( &back() );
        shrink_storage_to( size() - 1 );
    }

    //! <b>Effects</b>: Erases the element at position pos.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Linear to the elements between pos and the
    //!   last element. Constant if pos is the last element.
    iterator erase( const_iterator const position ) noexcept
    {
        verify_iterator( position );
        auto const pos_index{ index_of( position ) };
        auto const mutable_pos{ nth( pos_index ) };
        std::move( mutable_pos + 1, end(), mutable_pos );
        pop_back();
        return nth( pos_index );
    }

    //! <b>Effects</b>: Erases the elements pointed by [first, last).
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Linear to the distance between first and last
    //!   plus linear to the elements between pos and the last element.
    iterator erase( const_iterator const first, const_iterator const last ) noexcept
    {
        verify_iterator( first );
        verify_iterator( last  );
        BOOST_ASSERT( first <= last );
        auto const first_index{ index_of( first ) };
        if ( first != last ) [[ likely ]]
        {
            auto const mutable_start{ nth( first_index      ) };
            auto const mutable_end  { nth( index_of( last ) ) };
            auto const new_end      { std::move( mutable_end, end(), mutable_start ) };
            for ( auto & element : { new_end, end() } )
                std::destroy_at( &element );
            shrink_storage_to( static_cast<size_type>( new_end - begin() ) );
        }
        return nth( first_index );
    }

    //! <b>Effects</b>: Swaps the contents of *this and x.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    void swap( vector & x ) noexcept { swap( this->storage, x.storage ); }

    //! <b>Effects</b>: Erases all the elements of the vector.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Linear to the number of elements in the container.
    void clear() noexcept
    {
        for ( auto & element : span() )
            std::destroy_at( &element );
        shrink_storage_to( 0 );
    }

    //! <b>Effects</b>: Returns true if x and y are equal
    //!
    //! <b>Complexity</b>: Linear to the number of elements in the container.
    [[ nodiscard ]] friend bool operator==( vector const & x, vector const & y ) noexcept {  return std::equal( x.begin(), x.end(), y.begin(), y.end() ); }

    //! <b>Effects</b>: Returns true if x and y are unequal
    //!
    //! <b>Complexity</b>: Linear to the number of elements in the container.
    [[ nodiscard ]] friend bool operator!=(const vector& x, const vector& y) noexcept {  return !(x == y); }

    //! <b>Effects</b>: x.swap(y)
    //!
    //! <b>Complexity</b>: Constant.
    friend void swap( vector & x, vector & y ) noexcept { x.swap( y ); }

    ///////////////////////////////////////////////////////////////////////////
    // Extensions
    ///////////////////////////////////////////////////////////////////////////

    auto map_file  ( auto      const file, flags::named_object_construction_policy const policy ) noexcept { return storage_.map_file  ( file, policy ); }
    auto map_memory( size_type const size                                                       ) noexcept { return storage_.map_memory( size         ); }

    void close() noexcept { storage_.close(); }

    bool is_open() const noexcept { return static_cast<bool>( storage_ ); }

    //! <b>Effects</b>: If n is less than or equal to capacity(), this call has no
    //!   effect. Otherwise, it is a request for allocation of additional memory
    //!   (memory expansion) that will not invalidate iterators.
    //!   If the request is successful, then capacity() is greater than or equal to
    //!   n; otherwise, capacity() is unchanged. In either case, size() is unchanged.
    //!
    //! <b>Throws</b>: If memory allocation throws or T's copy/move constructor throws.
    //!
    //! <b>Note</b>: Non-standard extension.
    bool stable_reserve( size_type new_cap ) = /*TODO*/ delete;

    void grow_to( size_type const target_size, default_init_t ) requires( std::is_trivial_v<T> )
    {
        storage_.expand( to_byte_sz( target_size ) );
    }
    PSI_WARNING_DISABLE_PUSH()
    PSI_WARNING_MSVC_DISABLE( 4702 ) // unreachable code
    void grow_to( size_type const target_size, value_init_t )
    {
        auto const current_size{ size() };
        BOOST_ASSUME( target_size >= current_size );
        storage_.expand( to_byte_sz( target_size ) );
        if constexpr ( std::is_trivially_constructible_v<value_type> )
        {
            std::memset( data() + current_size, 0, target_size - current_size * sizeof( T ) );
        }
        else
        {
            for ( auto & element : span().subspan( current_size ) )
            {
                std::construct_at( &element );
            }
        }
    }
    PSI_WARNING_DISABLE_POP()
    void grow_by( size_type const delta, auto const init_policy )
    {
        grow_to( size() + delta, init_policy );
    }

    void shrink_to( size_type const target_size ) noexcept
    {
        auto const current_size{ size() };
        BOOST_ASSUME( target_size <= current_size );
        if ( !std::is_trivially_destructible_v<T> )
        {
            for ( auto & element : span().subspan( target_size ) )
                std::destroy_at( &element );
        }
        storage_.shrink( to_byte_sz( target_size ) );
    }
    void shrink_by( size_type const delta ) noexcept
    {
        shrink_to( size() - delta );
    }

    decltype( auto ) user_header_data() noexcept { return storage_.header_storage(); }

    // helper getter for generic code that wishes to do basic manipulation on
    // vm::vectors w/o being templated (contiguous_container_storage_base does not
    // publicize functionality that could be used to make it out of sync with the
    // corresponding vm::vector)
    contiguous_container_storage_base & storage_base() noexcept { return storage_; }

private:
    PSI_WARNING_DISABLE_PUSH()
    PSI_WARNING_GCC_OR_CLANG_DISABLE( -Wsign-conversion )
    static T *  to_t_ptr  ( mapped_view::value_type * const ptr     ) noexcept {                                             return reinterpret_cast< T * >( ptr ); }
    static sz_t to_t_sz   ( auto                      const byte_sz ) noexcept { BOOST_ASSUME( byte_sz % sizeof( T ) == 0 ); return static_cast< sz_t >( byte_sz / sizeof( T ) ); }
    static sz_t to_byte_sz( auto                      const sz      ) noexcept {                                             return static_cast< sz_t >(      sz * sizeof( T ) ); }
    PSI_WARNING_DISABLE_POP()

    void shrink_storage_to( size_type const target_size ) noexcept
    {
        storage_.shrink( to_byte_sz( target_size ) );
    }

    void verify_iterator( [[ maybe_unused ]] const_iterator const iter ) const noexcept
    {
        BOOST_ASSERT( iter >= begin() );
        BOOST_ASSERT( iter <= end  () );
    }

    iterator make_space_for_insert( const_iterator const position, size_type const n )
    {
        verify_iterator( position );
        auto const position_index{ index_of( position ) };
        auto const current_size  { size() };
        auto const new_size      { current_size + n };
        storage_.expand( to_byte_sz( new_size ) );
        auto const elements_to_move_uninitialized_space{ static_cast<size_type>( current_size - position_index            ) };
        auto const elements_to_move_to_the_current_end { static_cast<size_type>( n - elements_to_move_uninitialized_space ) };
        std::uninitialized_move_n( nth( current_size - elements_to_move_uninitialized_space ),                       elements_to_move_uninitialized_space , nth( static_cast<size_type>( new_size - elements_to_move_uninitialized_space ) ) );
        std::move                ( nth( position_index                                      ), nth( position_index + elements_to_move_to_the_current_end ), nth( static_cast<size_type>( new_size - n                                    ) ) );
        return nth( position_index );
    }
}; // class vector


template < typename T, typename sz_t, bool headerless >
#if defined( _MSC_VER ) && !defined( NDEBUG )
// Boost.Container flat_set is bugged: tries to dereference end iterators - asserts at runtime with secure/checked STL/containers
// https://github.com/boostorg/container/issues/261
requires is_trivially_moveable< T >
class unchecked_vector : public vector< T, sz_t, headerless >
{
public:
    using base = vector< T, sz_t, headerless >;

    using       iterator = typename base::      pointer;
    using const_iterator = typename base::const_pointer;

    using base::base;

    [[ nodiscard ]] auto  begin()       noexcept { return base::data(); }
    [[ nodiscard ]] auto  begin() const noexcept { return base::data(); }
    [[ nodiscard ]] auto cbegin() const noexcept { return begin(); }

    [[ nodiscard ]] auto  end()       noexcept { return begin() + base::size(); }
    [[ nodiscard ]] auto  end() const noexcept { return begin() + base::size(); }
    [[ nodiscard ]] auto cend() const noexcept { return end(); }

    iterator erase( const_iterator const first, const_iterator const last ) noexcept
    {
        return
            begin()
                +
            base::index_of
            (
                base::erase
                (
                    base::nth( static_cast<typename base::size_type>( first - cbegin() ) ),
                    base::nth( static_cast<typename base::size_type>( last  - cbegin() ) )
                )
            );
    }

    iterator erase( const_iterator const position ) noexcept
    {
        return
            begin()
                +
            base::index_of
            (
                base::erase
                (
                    base::cbegin() + ( position - cbegin() )
                )
            );
    }

    template < typename ... Args >
    iterator insert( const_iterator const position, Args &&... args ) noexcept
    {
        return
            begin()
                +
            base::index_of
            (
                base::insert
                (
                    base::nth( static_cast<typename base::size_type>( position - begin() ) ),
                    std::forward< Args >( args )...
                )
            );
    }
};
#else
using unchecked_vector = vector< T, sz_t, headerless >;
#endif

template < typename Vector >
using unchecked = unchecked_vector< typename Vector::value_type, typename Vector::size_type, Vector::headerless >;

//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------
