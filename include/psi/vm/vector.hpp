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

#include "mapping/mapping.hpp"
#include "mapped_view/mapped_view.hpp"
#include "mappable_objects/file/file.hpp"
#include "mappable_objects/file/utility.hpp"

#include <psi/build/disable_warnings.hpp>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <climits>
#include <span>
#include <type_traits>
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

    auto data()       noexcept { BOOST_ASSERT_MSG( !view_.empty(), "Paging file not attached" ); return view_.data(); }
    auto data() const noexcept { return const_cast< contiguous_container_storage_base & >( *this ).data(); }

    void unmap() noexcept { view_.unmap(); }

    explicit operator bool() const noexcept { return static_cast<bool>( mapping_ ); }

protected:
    static std::uint8_t constexpr header_size{ 64 };

    constexpr contiguous_container_storage_base() = default;

    err::fallible_result< std::size_t, error > open( auto const * const file_name ) noexcept { return open( create_file( file_name, create_rw_file_flags() ) ); }

    [[ gnu::pure ]] auto storage_size() const noexcept { return get_size( mapping_ ); }
    [[ gnu::pure ]] auto  mapped_size() const noexcept { return view_.size(); }
#
    void expand( std::size_t const target_size )
    {
        set_size( mapping_, target_size );
        view_.expand( target_size, mapping_ );
    }

    void shrink( std::size_t const target_size ) noexcept
    {
        view_.shrink( target_size );
        set_size( mapping_, target_size )().assume_succeeded();
    }

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

    err::fallible_result< std::size_t, error > open( file_handle && file ) noexcept
    {
        if ( !file )
            return error{};
        auto const file_size{ get_size( file ) };
        BOOST_ASSERT_MSG( file_size <= std::numeric_limits<std::size_t>::max(), "Pagging file larger than address space!?" );
        auto const existing_size{ static_cast<std::size_t>( file_size ) };
        bool const created_file { existing_size == 0 };
        BOOST_ASSERT_MSG( existing_size >= header_size || created_file, "Corrupted file: bogus on-disk size" );
        auto const mapping_size { std::max<std::size_t>( header_size, existing_size ) };
        if ( created_file && !mapping::create_mapping_can_set_source_size )
            set_size( file, mapping_size );

        using ap    = flags::access_privileges;
        using flags = flags::mapping;
        mapping_ = create_mapping
        (
            std::move( file ),
            ap::object{ ap::readwrite },
            ap::child_process::does_not_inherit,
            flags::share_mode::shared,
            mapping_size
        );
        if ( !mapping_ )
            return error{};

        view_ = mapped_view::map( mapping_, 0, mapping_size );
        if ( !view_.data() )
            return error{};

        return std::size_t{ mapping_size };
    }

private:
    mapping     mapping_;
    mapped_view view_   ;
}; // contiguous_container_storage_base

template < typename sz_t >
class contiguous_container_storage : public contiguous_container_storage_base
{
public:
    using size_type = sz_t;

private:
    struct alignas( header_size ) header
    {
        std::byte user_storage[ header_size - sizeof( size_type ) ];
        size_type size;
    };

public:
    err::fallible_result< void, error > open( auto const * const file_name ) noexcept
    {
        auto const sz{ contiguous_container_storage_base::open( create_file( file_name, create_rw_file_flags() ) )() };
        if ( !sz )
            return sz.error();
        auto const stored_size{ hdr().size };
        BOOST_ASSERT_MSG( stored_size <= *sz - header_size, "Corrupted file: stored size larger than the file itself?" );
        // Clamp bogus/too large sizes (implicitly handles possible garbage on file creation).
        hdr().size = std::min( stored_size, static_cast<size_type>( *sz - header_size ) );
        return err::success;
    }

    auto data()       noexcept { return contiguous_container_storage_base::data() + header_size; }
    auto data() const noexcept { return contiguous_container_storage_base::data() + header_size; }

    auto & hdr_storage() noexcept { return hdr().user_storage; }

    [[ gnu::pure, nodiscard ]] auto storage_size() const noexcept { return static_cast<size_type>( contiguous_container_storage_base::storage_size() ); }
    [[ gnu::pure, nodiscard ]] auto  mapped_size() const noexcept { return static_cast<size_type>( contiguous_container_storage_base:: mapped_size() ); }

    [[ gnu::pure, nodiscard ]] auto size    () const noexcept { return hdr().size                 ; }
    [[ gnu::pure, nodiscard ]] auto capacity() const noexcept { return mapped_size() - header_size; }

    void expand( size_type const target_size )
    {
        BOOST_ASSUME( target_size > size() );
        if ( target_size > capacity() ) [[ likely ]]
            contiguous_container_storage_base::expand( target_size + header_size );
        hdr().size = target_size;
    }

    void shrink( size_type const target_size ) noexcept
    {
        contiguous_container_storage_base::shrink( target_size + header_size );
        hdr().size = target_size;
    }

    void resize( size_type const target_size )
    {
        if ( target_size > size() )
        {
            if ( target_size > capacity() )
                expand( target_size );
        }
        else
        {
            // or skip this like std::vector and rely on an explicit shrink_to_fit() call?
            shrink( target_size );
        }
        hdr().size = target_size;
    }

    void reserve( size_type const new_capacity )
    {
        if ( new_capacity > capacity() )
            contiguous_container_storage_base::expand( new_capacity + header_size );
    }

    void shrink_to_fit() noexcept
    {
        contiguous_container_storage_base::shrink( hdr().size + header_size );
    }

    bool has_extra_capacity() const noexcept
    {
        BOOST_ASSERT( size() <= capacity() );
        return size() != capacity();
    }

    void allocate_available_capacity( size_type const sz ) noexcept
    {
        BOOST_ASSERT_MSG( sz <= ( capacity() - size() ), "Out of preallocated space" );
        hdr().size += sz;
    }

private:
    [[ gnu::pure, nodiscard ]] auto       & hdr()       noexcept { return *reinterpret_cast< header       * >( contiguous_container_storage_base::data() ); }
    [[ gnu::pure, nodiscard ]] auto const & hdr() const noexcept { return *reinterpret_cast< header const * >( contiguous_container_storage_base::data() ); }
}; // contiguous_container_storage

template < typename T >
bool constexpr is_trivially_moveable
{
#ifdef __clang__
    __is_trivially_relocatable( T ) ||
#endif
    std::is_trivially_move_constructible_v< T >
};

struct default_init_t {}; inline constexpr default_init_t default_init;
struct value_init_t   {}; inline constexpr value_init_t   value_init  ;

// Standard-vector-like/compatible _presistent_ container class template
// which uses VM/a mapped object for its backing storage. Currently limited
// to trivial_abi types.
// Used the Boost.Container vector as a starting skeleton.

template < typename T, typename sz_t = std::size_t >
requires is_trivially_moveable< T >
class vector
{
private:
    contiguous_container_storage< sz_t > storage_;

public:
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
    vector() noexcept {}

    //! <b>Effects</b>: Constructs a vector taking the allocator as parameter.
    //!
    //! <b>Throws</b>: Nothing
    //!
    //! <b>Complexity</b>: Constant.
    explicit vector( contiguous_container_storage< sz_t > && storage ) noexcept : storage_{ std::move( storage ) } {}

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
        for ( ; first != last && cur != end_it; ++cur, ++first)
            *cur = *first;

        if ( first == last )
        {
            //There are no more elements in the sequence, erase remaining
            while ( cur != end_it )
                (*cur++).~value_type();
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
    [[ nodiscard ]] bool empty() const noexcept { return span().empty(); }

    //! <b>Effects</b>: Returns the number of the elements contained in the vector.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] size_type size() const noexcept { return to_t_sz( storage_.size() ); }

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
    PSI_WARNING_DISABLE_PUSH()
    PSI_WARNING_MSVC_DISABLE( 4702 ) // unreachable code
    void resize( size_type const new_size, value_init_t )
    {
        auto const current_size{ size() };
        if ( new_size < current_size && !std::is_trivially_destructible_v< T > )
        {
            for ( auto & element : span().subspan( current_size ) )
                std::destroy_at( &element );
        }
        storage_.resize( to_byte_sz( new_size ) );
        if ( new_size < current_size )
            return;
        if constexpr ( std::is_trivially_constructible_v< value_type > )
            std::memset( data() + current_size, 0, new_size - current_size * sizeof( T ) );
        else
        {
            for ( auto & element : span().subspan( current_size ) )
                std::construct_at( &element );
        }
    }
    PSI_WARNING_DISABLE_POP()

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
    [[ nodiscard ]] const_reference back()  const noexcept { return span().back(); }

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
        return begin() + n;
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
        return begin() + n;
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
        BOOST_ASSERT( p <= end() );
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
        BOOST_ASSERT( p <= end() );
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
        auto const mutable_pos{ nth( index_of( position ) ) };
        std::move( mutable_pos + 1, end(), mutable_pos );
        pop_back();
        return nth( index_of( position ) );
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

    auto open( auto const file ) { return storage_.open( file ); }

    bool is_open() const noexcept { return static_cast<bool>( storage_ ); }

    decltype( auto ) user_header_data() noexcept { return storage_.hdr_storage(); }

    //! <b>Effects</b>: If n is less than or equal to capacity(), this call has no
    //!   effect. Otherwise, it is a request for allocation of additional memory
    //!   (memory expansion) that will not invalidate iterators.
    //!   If the request is successful, then capacity() is greater than or equal to
    //!   n; otherwise, capacity() is unchanged. In either case, size() is unchanged.
    //!
    //! <b>Throws</b>: If memory allocation allocation throws or T's copy/move constructor throws.
    //!
    //! <b>Note</b>: Non-standard extension.
    bool stable_reserve( size_type new_cap ) = /*TODO*/ delete;

    // TODO grow shrink

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


template < typename T, typename sz_t = std::size_t >
#if defined( _MSC_VER ) && !defined( NDEBUG )
// Boost.Container flat_set is bugged: tries to get the reference from end iterators - asserts at runtime with secure/checked STL/containers
// https://github.com/boostorg/container/issues/261
requires is_trivially_moveable< T >
class unchecked_vector : public vector< T, sz_t >
{
public:
    using base = vector< T, sz_t >;

    using       iterator = typename base::      pointer;
    using const_iterator = typename base::const_pointer;

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
                    base::nth( static_cast<typename base::size_type>( first - begin() ) ),
                    base::nth( static_cast<typename base::size_type>( last  - begin() ) )
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
                    base::nth( static_cast<typename base::size_type>( position - begin() ) )
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
using unchecked_vector = vector< T, sz_t >;
#endif

template < typename Vector >
using unchecked = unchecked_vector< typename Vector::value_type, typename Vector::size_type >;

//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------
