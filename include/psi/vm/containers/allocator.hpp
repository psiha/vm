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

#include <cstdint>
#include <climits>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace detail { [[ noreturn, gnu::cold ]] void throw_bad_alloc(); }

class allocator_backing_mapping
{
public:
    constexpr allocator_backing_mapping() = default;

    err::fallible_result<std::size_t, error> open( auto const * const file_name ) noexcept { return open( create_file( file_name, create_rw_file_flags() ) ); }

    auto data() const noexcept { return view_.data(); }

    [[ gnu::pure ]] auto        size() const noexcept { return get_size( mapping_ ); }
    [[ gnu::pure ]] auto mapped_size() const noexcept { return view_.size(); }

    mapped_span::pointer initial_allocation( std::size_t const sz ) noexcept
    {
        BOOST_ASSERT_MSG( view_.empty()     , "First allocation already performed" );
#   if 0
        BOOST_ASSERT_MSG( this->size() == sz, "Persistent-file-backed-allocator hack: assuming first allocation to be for the entire file" );

        auto maybe_view{ view_.map( mapping_, 0, sz ).as_result_or_error() };
        if ( !maybe_view )
            return nullptr;
        view_ = std::move( *maybe_view );
        return view_.data();
#   else
        if ( expand( sz ) )
            return view_.data();
        else
            return nullptr;
#   endif
    }

    bool expand( std::size_t const target_size ) noexcept
    {
        auto const success{ set_size( mapping_, target_size )().succeeded() };
        return success && view_.expand
        (
            target_size, mapping_
        )().succeeded();
    }

    void shrink( std::size_t const target_size ) noexcept
    {
        set_size( mapping_, target_size )().assume_succeeded();
        view_.shrink( target_size );
    }

    void unmap() noexcept { view_.unmap(); }

private:
    err::fallible_result< std::size_t, error > open( file_handle && file ) noexcept
    {
        if ( !file )
            return error{};
        auto const file_size{ get_size( file ) };
        BOOST_ASSERT_MSG( file_size <= std::numeric_limits<std::size_t>::max(), "Pagging file larger than address space!?" );
        auto const existing_size{ static_cast<std::size_t>( file_size ) };

        using ap    = flags::access_privileges;
        using flags = flags::mapping;
        mapping_ = create_mapping
        (
            std::move( file ),
            ap::object{ ap::readwrite },
            ap::child_process::does_not_inherit,
            flags::share_mode::shared,
            existing_size
        );
        if ( !mapping_ )
            return error{};

        view_ = mapped_view::map( mapping_, 0, existing_size );
        if ( !view_.data() )
            return error{};

        return std::size_t{ existing_size };
    }

private:
    mapping     mapping_;
    mapped_view view_   ;
}; // allocator_backing_mapping

// WiP/toy attempt to provide an "extended STL-compatible allocator that
// offers advanced allocation mechanisms (in-place expansion, shrinking..)"
// that would work specifically with Boost.Container contiguous/'flat'
// containers (i.e. expect only a single allocation that is later only resized).
// So far the attempt fails because Boost.Container expect that a successful
// expand_bwd allocation command implies that the start/base address of the
// allocation has not moved yet this need not hold (is not necessary) for a VM
// based allocator - as memory can be remapped to a new location without any
// copying actually taking place (a safe operation for trivial_abi types).
// Similarily the expand_fwd allocation has a similar expectation which again
// need not hold.
// https://github.com/boostorg/container/issues/260
//
// https://arxiv.org/pdf/2108.07223.pdf Metall: A Persistent Memory Allocator For Data-Centric Analytics
// https://my.eng.utah.edu/~cs4400/malloc.pdf
// https://github.com/templeblock/mmap_allocator

template < typename T, typename sz_t = std::size_t >
class allocator
{
private:
    using allocation_commands = std::uint8_t;

    allocator_backing_mapping * p_storage_{ nullptr };

    static T *  to_t_ptr( mapped_view::value_type * const ptr ) noexcept { return reinterpret_cast<T *>( ptr ); }
    static sz_t  to_t_sz( auto                      const sz  ) noexcept { return static_cast<sz_t>( sz / sizeof( T ) ); }

public:
    using value_type = T;
    using       pointer = T *;
    using const_pointer = T const *;
    using       reference = T &;
    using const_reference = T const &;
    using       size_type = sz_t;
    using difference_type = std::make_signed_t<size_type>;

    using version = boost::container::dtl::version_type<allocator, 2>;

#if 0 // not supported
    using void_multiallocation_chain = boost::container::dtl::basic_multiallocation_chain<void *>;
    using      multiallocation_chain = boost::container::dtl::transform_multiallocation_chain<void_multiallocation_chain, T>;
#endif

    //!Obtains an allocator that allocates
    //!objects of type T2
    template<class T2>
    struct rebind
    {
        typedef allocator<T2, sz_t/*, Version, AllocationDisableMask*/> other;
    };

    //!Default constructor
    //!Never throws
    constexpr allocator( allocator_backing_mapping * const p_storage = nullptr ) noexcept : p_storage_{ p_storage } {}

    constexpr allocator( allocator       && other ) noexcept { swap( other ); }
    constexpr allocator( allocator const &  other ) noexcept : p_storage_{ other.p_storage_ } {}

    //!Allocates memory for an array of count elements.
    //!Throws bad_alloc if there is no enough memory
    //!If Version is 2, this allocated memory can only be deallocated
    //!with deallocate() or (for Version == 2) deallocate_many()
    [[ nodiscard ]] pointer allocate( size_type const count, [[ maybe_unused ]] void const * const hint = nullptr )
    {
        BOOST_ASSERT_MSG( p_storage_, "No attached storage." );

        if ( count > max_size() )
            detail::throw_bad_alloc();
        auto const result{ p_storage_->initial_allocation( count * sizeof( T ) ) };
        if ( !result ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return to_t_ptr( result );
    }

    //!Deallocates previously allocated memory.
    //!Never throws
    void deallocate( pointer const ptr, size_type const size ) noexcept
    {
        BOOST_VERIFY( to_t_ptr( p_storage_->data() ) == ptr && p_storage_->mapped_size() == size );
        p_storage_->unmap();
    }

    //!Returns the maximum number of elements that could be allocated.
    //!Never throws
    static constexpr size_type max_size() noexcept { return std::numeric_limits<size_type>::max() / sizeof( T ); }

    //!Swaps two allocators, does nothing
    //!because this allocator is stateless
    friend void swap( allocator & left, allocator & right ) noexcept
    {
        using namespace std;
        swap( left.p_storage_, right.p_storage_ );
    }

    //!An allocator always compares to true, as memory allocated with one
    //!instance can be deallocated by another instance
    friend bool operator==( allocator const & left, allocator const & right ) noexcept
    {
        return left.p_storage_ == right.p_storage_;
    }

    //!An advanced function that offers in-place expansion shrink to fit and new allocation
    //!capabilities. Memory allocated with this function can only be deallocated with deallocate()
    //!or deallocate_many().
    // https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2006/n2045.html
    [[ nodiscard ]] pointer allocation_command
    (
        allocation_commands const command,
        size_type           const limit_size,
        size_type & prefer_in_recvd_out_size,
        pointer   & reuse
    )
    {
        BOOST_ASSERT_MSG( p_storage_, "No assigned storage" );

        namespace bc = boost::container;

        BOOST_ASSERT_MSG( !( command & bc::expand_bwd  ) || ( command & bc::expand_fwd ), "Unimplemented command" );
        BOOST_ASSERT_MSG( !( command & bc::zero_memory )                                , "Unimplemented command" );
        BOOST_ASSERT_MSG( !!( command & bc::shrink_in_place ) != !!( command & ( bc::allocate_new | bc::expand_fwd | bc::expand_bwd ) ), "Conflicting commands" );

        BOOST_ASSERT( !reuse || reuse == to_t_ptr( p_storage_->data() ) );
        auto const preferred_size     { prefer_in_recvd_out_size };
        auto const preferred_byte_size{ static_cast<size_type>( preferred_size * sizeof( T ) ) };
        auto const current_size       { to_t_sz( p_storage_->mapped_size() ) };
        bool success{ false };
        if ( reuse && ( command & bc::expand_fwd ) )
        {
            BOOST_ASSUME( preferred_size >= current_size );
            success = p_storage_->expand( preferred_byte_size );
            BOOST_ASSUME( to_t_sz( p_storage_->mapped_size() ) <= limit_size );
        }
        else
        if ( reuse && ( command & ( bc::shrink_in_place | bc::try_shrink_in_place ) ) )
        {
            BOOST_ASSUME( preferred_size <= current_size );
            p_storage_->shrink( preferred_byte_size );
            BOOST_ASSUME( to_t_sz( p_storage_->mapped_size() ) >= limit_size );
            BOOST_ASSERT( reuse == to_t_ptr( p_storage_->data() ) );
            success = true;
        }
        else
        if ( command & bc::allocate_new )
        {
            auto ptr{ p_storage_->initial_allocation( preferred_byte_size ) };
            if ( ptr ) [[ likely ]]
            {
                BOOST_ASSERT( ptr == p_storage_->data() );
                reuse   = nullptr;
                success = true;
            }
        }
        else
        {
            BOOST_UNREACHABLE();
        }

        if ( success ) [[ likely ]]
        {
            reuse                    = reuse ? to_t_ptr( p_storage_->data       () ) : nullptr;
            prefer_in_recvd_out_size =         to_t_sz ( p_storage_->mapped_size() )          ;
            return to_t_ptr( p_storage_->data() );
        }

        if ( !( command & bc::nothrow_allocation ) )
            boost::container::throw_bad_alloc();

        return nullptr;
    }

    //!Returns maximum the number of objects the previously allocated memory
    //!pointed by p can hold.
    //!Memory must not have been allocated with
    //!allocate_one or allocate_individual.
    //!This function is available only with Version == 2
    [[ nodiscard ]] size_type size( pointer const p ) const noexcept
    {
        BOOST_VERIFY( p == p_storage_->data() );
        return p_storage_->mapped_size();
    }

#if 0 // not implemented/supported/needed yet
    //!Allocates just one object. Memory allocated with this function
    //!must be deallocated only with deallocate_one().
    //!Throws bad_alloc if there is no enough memory
    [[ nodiscard ]] pointer allocate_one() = delete;

    //!Allocates many elements of size == 1.
    //!Elements must be individually deallocated with deallocate_one()
    void allocate_individual( std::size_t num_elements, multiallocation_chain & chain ) = delete;

    //!Deallocates memory previously allocated with allocate_one().
    //!You should never use deallocate_one to deallocate memory allocated
    //!with other functions different from allocate_one() or allocate_individual.
    //Never throws
    void deallocate_one( pointer p ) noexcept = delete;

    //!Deallocates memory allocated with allocate_one() or allocate_individual().
    //!This function is available only with Version == 2
    void deallocate_individual( multiallocation_chain & chain ) noexcept = delete;

    //!Allocates many elements of size elem_size.
    //!Elements must be individually deallocated with deallocate()
    void allocate_many( size_type elem_size, std::size_t n_elements, multiallocation_chain & chain ) = delete;

    //!Allocates n_elements elements, each one of size elem_sizes[i]
    //!Elements must be individually deallocated with deallocate()
    void allocate_many( const size_type * elem_sizes, size_type n_elements, multiallocation_chain & chain ) = delete;

    //!Deallocates several elements allocated by
    //!allocate_many(), allocate(), or allocation_command().
    void deallocate_many( multiallocation_chain & chain ) noexcept = delete;
#endif
}; // class allocator

//------------------------------------------------------------------------------
} // namespace namespace psi::vm
//------------------------------------------------------------------------------
