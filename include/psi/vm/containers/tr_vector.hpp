////////////////////////////////////////////////////////////////////////////////
/// Trivially relocatable vector
/// 
/// I.e a vector for trivially relocatable types - a thin std::vector
/// replacement, built around the CRT and/or low level OS allocation APIs,
/// designed for trivially moveable/'relocatable' types (eliminating the double
/// allocation and copy-on-resize overhead of std::vector) with emphasis on
/// minimizing bloat + vector_impl extensions.
/// TODO: expand/finish support for non trivially_moveable types and rename to
/// simply vector
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

#include <boost/assert.hpp>
#include <boost/container/detail/allocation_type.hpp>
#include <boost/container/detail/version_type.hpp>

#include <cstdlib>
#include <memory>
#include <utility>

#ifdef __linux__
#include <malloc.h>
#elif defined( __APPLE__ )
#include <malloc/malloc.h>
#endif
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace detail
{
    // https://www.gnu.org/software/libc/manual/html_node/Aligned-Memory-Blocks.html
    inline std::uint8_t constexpr guaranteed_alignment{ 16 }; // all known x64 and arm64 platforms

    [[ gnu::pure ]] inline std::size_t crt_alloc_size( void const * const address ) noexcept
    {
        // https://lemire.me/blog/2017/09/15/how-fast-are-malloc_size-and-malloc_usable_size-in-c
#   if defined( _MSC_VER )
        // https://masm32.com/board/index.php?topic=7018.0 HeapAlloc vs HeapReAlloc
        return _msize( const_cast<void *>( address ) ); // uber slow & almost useless as it returns the requested alloc size, not the capacity of the allocated block (it just calls HeapSize)
#   elif defined( __linux__ )
        return ::malloc_usable_size( const_cast<void *>( address ) ); // fast
#   elif defined( __APPLE__ )
        return ::malloc_size( address ); // not so fast
#   else
        static_assert( false, "no malloc size implementation" );
#   endif
    }
    template <std::uint8_t alignment>
    std::size_t crt_aligned_alloc_size( void const * const address ) noexcept
    {
#   if defined( _MSC_VER )
        if constexpr ( alignment > guaranteed_alignment )
            return _aligned_msize( const_cast<void *>( address ), alignment, 0 );
#   endif
        return crt_alloc_size( address );
    }

    // From GCC docs: realloc-like functions have this property (malloc/restrict) as long as the old pointer is never referred to (including comparing it to the new pointer) after the function returns a non-NULL value.
    [[ using gnu: cold, assume_aligned( guaranteed_alignment ), malloc ]]
#ifdef _MSC_VER
    __declspec( restrict, noalias )
#endif
    inline void * crt_realloc( void * const existing_allocation_address, std::size_t const new_size )
    {
        auto const new_allocation{ std::realloc( existing_allocation_address, new_size ) };
        if ( ( new_allocation == nullptr ) && ( new_size != 0 ) ) [[ unlikely ]]
            throw_bad_alloc();
        return new_allocation;
    }

    [[ using gnu: cold, assume_aligned( guaranteed_alignment ), malloc, ]]
#ifdef _MSC_VER
    __declspec( noalias, restrict )
#endif
    inline void * crt_aligned_realloc( void * const existing_allocation_address, std::size_t const existing_allocation_size, std::size_t const new_size, std::uint8_t const alignment )
    {
        BOOST_ASSUME( alignment > guaranteed_alignment );
#   if defined( _MSC_VER )
        std::ignore = existing_allocation_size;
        auto const new_allocation{ ::_aligned_realloc( existing_allocation_address, new_size, alignment ) };
#   else
        void * new_allocation{ nullptr };
        if ( existing_allocation_address ) [[ likely ]]
        {
            auto const try_realloc{ std::realloc( existing_allocation_address, new_size ) };
            if ( is_aligned( try_realloc, alignment ) ) [[ likely ]]
            {
                new_allocation = try_realloc;
            }
            else
            {
                BOOST_ASSUME( try_realloc ); // nullptr handled implicitly above
                if ( posix_memalign( &new_allocation, alignment, new_size ) == 0 ) [[ likely ]]
                    std::memcpy( new_allocation, try_realloc, existing_allocation_size );
                std::free( try_realloc );
            }
        }
        else
        {
            BOOST_ASSUME( !existing_allocation_size );
            // "On Linux (and other systems), posix_memalign() does not modify
            // memptr on failure. A requirement standardizing this behavior was
            // added in POSIX .1 - 2008 TC2."
            BOOST_VERIFY( posix_memalign( &new_allocation, alignment, new_size ) == 0 );
        }
#   endif
        if ( ( new_allocation == nullptr ) && ( new_size != 0 ) ) [[ unlikely ]]
            throw_bad_alloc();
        return new_allocation;
    } // crt_aligned_realloc()

    template <std::uint8_t alignment>
    [[ using gnu: assume_aligned( alignment ), malloc ]]
#ifdef _MSC_VER
    __declspec( noalias, restrict )
#endif
    void * crt_realloc( void * const existing_allocation_address, std::size_t const existing_allocation_size, std::size_t const new_size )
    {
        if constexpr ( alignment > guaranteed_alignment )
            return crt_aligned_realloc( existing_allocation_address, existing_allocation_size, new_size, alignment );
        else
            return crt_realloc( existing_allocation_address, new_size );
    }
#ifdef _MSC_VER
    __declspec( noalias )
#endif
    inline
    void crt_aligned_free( void * const allocation ) noexcept
    {
#   if __has_builtin( __builtin_constant_p )
        if ( __builtin_constant_p( allocation ) && !allocation )
            return;
#   endif
#   if defined( _MSC_VER )
        ::_aligned_free( allocation );
#   else
        std::free( allocation );
#   endif
    }

    template <typename T, bool> struct capacity           { T value; };
    template <typename T      > struct capacity<T, false> {
        constexpr capacity() = default;
        constexpr capacity( std::nullptr_t ) noexcept {}
    };
} // namespace detail

template <typename T, typename sz_t = std::size_t, std::uint8_t alignment = alignof( T )>
struct crt_aligned_allocator
{
    using value_type      = T;
    using       pointer   = T *;
    using const_pointer   = T const *;
    using       reference = T &;
    using const_reference = T const &;
    using       size_type = sz_t;
    using difference_type = std::make_signed_t<size_type>;

    using allocation_commands = std::uint8_t;
    using version = boost::container::dtl::version_type<crt_aligned_allocator, 2>;

    template <class U> struct rebind { using other = crt_aligned_allocator<U, sz_t, alignment>; };

    //!Allocates memory for an array of count elements.
    //!Throws bad_alloc if there is no enough memory
    //!If Version is 2, this allocated memory can only be deallocated
    //!with deallocate() or (for Version == 2) deallocate_many()
    [[ nodiscard ]]
    [[ using gnu: cold, assume_aligned( alignment ), malloc, returns_nonnull ]]
#ifdef _MSC_VER
    __declspec( restrict, noalias )
#endif
    static pointer allocate( size_type const count, [[ maybe_unused ]] void const * const hint = nullptr )
    {
        BOOST_ASSUME( count < max_size() );
        auto const byte_size{ count * sizeof( T ) };
        void * new_allocation{ nullptr };
        if constexpr ( alignment > detail::guaranteed_alignment )
        {
#       if defined( _MSC_VER )
            new_allocation = ::_aligned_malloc( byte_size, alignment );
#       else
            BOOST_VERIFY( posix_memalign( &new_allocation, alignment, byte_size ) == 0 );
#       endif
        }
        else
        {
            new_allocation = std::malloc( byte_size );
        }

        if ( !new_allocation ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return std::assume_aligned<alignment>( static_cast<pointer>( new_allocation ) );
    }
#if __cpp_lib_allocate_at_least >= 202306L
    static auto allocate_at_least( size_type const count )
    {
        auto const ptr{ allocate( count ) };
        return std::allocation_result{ ptr, size( ptr ) };
    }
#endif

    //!Deallocates previously allocated memory.
    //!Never throws
    static void deallocate( pointer const ptr, [[ maybe_unused ]] size_type const size = 0 ) noexcept
    {
#   if __has_builtin( __builtin_constant_p )
        if ( __builtin_constant_p( ptr ) && !ptr )
            return;
#   endif
        if constexpr ( alignment > detail::guaranteed_alignment )
            return detail::crt_aligned_free( ptr );
        else
            return std::free( ptr );
    }

    [[ nodiscard ]] static pointer resize( pointer const current_address, size_type const current_size, size_type const target_size )
    {
        auto const result_address{ do_resize( current_address, current_size, target_size ) };
        if ( !result_address ) [[ unlikely ]]
            detail::throw_bad_alloc();
        return result_address;
    }
    [[ nodiscard ]] static pointer grow_to( pointer const current_address, size_type const current_size, size_type const target_size )
    {
        BOOST_ASSUME( target_size >= current_size );
        return resize( current_address, current_size, target_size );
    }
    [[ nodiscard ]] static pointer shrink_to( pointer const current_address, size_type const current_size, size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= current_size );
        return do_resize( current_address, current_size, target_size );
    }

    //!Returns the maximum number of elements that could be allocated.
    //!Never throws
    [[ gnu::const ]] static constexpr size_type max_size() noexcept { return std::numeric_limits<size_type>::max() / sizeof( T ); }

    //!An advanced function that offers in-place expansion shrink to fit and new allocation
    //!capabilities. Memory allocated with this function can only be deallocated with deallocate()
    //!or deallocate_many().
    // https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2006/n2045.html
    [[ nodiscard ]] static pointer allocation_command
    (
        allocation_commands const command,
        [[ maybe_unused ]]
        size_type           const limit_size,
        size_type & prefer_in_recvd_out_size,
        pointer   & reuse
    )
    {
        namespace bc = boost::container;

        BOOST_ASSERT_MSG( !( command & bc::zero_memory ), "Unimplemented command" );
        BOOST_ASSERT_MSG( !!( command & bc::shrink_in_place ) != !!( command & ( bc::allocate_new | bc::expand_fwd | bc::expand_bwd ) ), "Conflicting commands" );

        auto const preferred_size     { prefer_in_recvd_out_size };
        auto const preferred_byte_size{ static_cast<size_type>( preferred_size * sizeof( T ) ) };
        auto const current_size       { reuse ? size( reuse ) : 0 };
        bool success{ false };
#   ifdef _MSC_VER  // no try_realloc for others so this cannot be safely implemented
        // TODO:
        //  - Linux: switch to mmap+mremap for non trivial_abi types
        //  - OSX https://stackoverflow.com/questions/72637850/how-to-use-virtual-memory-implement-realloc-on-mac-osx
        if ( reuse && ( command & bc::expand_fwd ) && ( alignment <= detail::guaranteed_alignment ) )
        {
            BOOST_ASSUME( preferred_size >= current_size );
            auto const expand_result{ ::_expand( reuse, preferred_byte_size ) };
            if ( expand_result )
            {
                BOOST_ASSUME( reuse == expand_result );
                success = true;
            }
        }
        else
#   endif
        if ( reuse && ( command & ( bc::shrink_in_place | bc::try_shrink_in_place ) ) )
        {
            BOOST_ASSUME( preferred_size <= current_size );
            auto const new_address{ static_cast<pointer>( std::realloc( reuse, preferred_byte_size ) ) };
            BOOST_ASSUME( new_address == reuse );
            reuse   = new_address;
            success = true;
        }
        else
        if ( command & bc::allocate_new )
        {
            reuse   = allocate( preferred_size );
            success = true; // allocate() throws
        }
        else
        {
            std::unreachable();
        }

        if ( success ) [[ likely ]]
        {
            BOOST_ASSUME( reuse );
            prefer_in_recvd_out_size = size( reuse );
            return reuse;
        }

        if ( !( command & bc::nothrow_allocation ) )
            detail::throw_bad_alloc();

        return nullptr;
    }

    //!Returns the maximum number of objects the previously allocated memory
    //!pointed by p can hold.
    [[ nodiscard, gnu::pure ]] static size_type size( const_pointer const p ) noexcept
    {
        return static_cast<size_type>( detail::crt_aligned_alloc_size<alignment>( p ) / sizeof( T ) );
    }

    //!Allocates just one object. Memory allocated with this function
    //!must be deallocated only with deallocate_one().
    //!Throws bad_alloc if there is no enough memory
    [[ nodiscard ]] static pointer allocate_one() { return allocate( 1 ); }
    //!Deallocates memory previously allocated with allocate_one().
    //!You should never use deallocate_one to deallocate memory allocated
    //!with other functions different from allocate_one() or allocate_individual.
    //Never throws
    static void deallocate_one( pointer const p ) noexcept { return deallocate( p, 1 ); }

private:
    [[ gnu::cold, gnu::assume_aligned( alignment ) ]]
    [[ nodiscard ]] static pointer do_resize( pointer const existing_allocation_address, size_type const existing_allocation_size, size_type const new_size )
    {
        return std::assume_aligned<alignment>( static_cast<pointer>(
            detail::crt_realloc<alignment>( existing_allocation_address, existing_allocation_size * sizeof( T ), new_size * sizeof( T ) )
        ));
    }
#if 0 // not implemented/supported/needed yet
    using void_multiallocation_chain = boost::container::dtl::basic_multiallocation_chain<void *>;
    using      multiallocation_chain = boost::container::dtl::transform_multiallocation_chain<void_multiallocation_chain, T>;

    //!Allocates many elements of size == 1.
    //!Elements must be individually deallocated with deallocate_one()
    void allocate_individual( std::size_t num_elements, multiallocation_chain & chain ) = delete;

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
}; // class crt_aligned_allocator


struct tr_vector_options
{
    std::uint8_t alignment                { 0    }; // 0 -> default
    bool         cache_capacity           { true }; // if your crt_alloc_size is slow (MSVC)
    bool         explicit_geometric_growth{ true }; // if your realloc impl is slow (yes MSVC we are looking at you again)
}; // struct tr_vector_options

template <typename T, typename sz_t = std::size_t, tr_vector_options options = {}>
requires( is_trivially_moveable<T> )
class [[ nodiscard, clang::trivial_abi ]] tr_vector
    :
    public vector_impl<tr_vector<T, sz_t, options>, T, sz_t>
{
public:
    static std::uint8_t constexpr alignment{ options.alignment ? options.alignment : std::uint8_t{ alignof( std::conditional_t<complete<T>, T, std::max_align_t> ) } };

    static bool constexpr storage_zero_initialized{ false };

    using size_type      = sz_t;
    using value_type     = T;
    using allocator_type = crt_aligned_allocator<T, sz_t, alignment>;

private:
    using al   = allocator_type;
    using base = vector_impl<tr_vector<T, sz_t, options>, T, sz_t>;

public:
    using base::base;
    // cannot use a defaulted constructor with default member initializers as they would overwrite the values stored by storage_init called by base()
    constexpr tr_vector() noexcept : p_array_{ nullptr }, size_{ 0 }, capacity_{ 0 } {}
    constexpr tr_vector( tr_vector const & other )
    {
        auto const data{ storage_init( other.size() ) };
        try { std::uninitialized_copy_n( other.data(), other.size(), data ); }
        catch(...) { storage_free(); throw; }
    }
    constexpr tr_vector( tr_vector && other ) noexcept : p_array_{ other.p_array_ }, size_{ other.size_ }, capacity_{ other.capacity_ } { other.mark_freed(); }

    constexpr tr_vector & operator=( tr_vector const & other ) { return ( *this = tr_vector( other ) ); }
    constexpr tr_vector & operator=( tr_vector && other ) noexcept
    {
        // Swap contents: other's destructor frees our old allocation.
        // (but first clear - to avoid surprising callers with leaving
        // 'something' in other, i.e. leave only capcaity)
        this->clear();
        this->p_array_  = std::exchange( other.p_array_ , this->p_array_  );
        this->size_     = std::exchange( other.size_    , this->size_     );
        this->capacity_ = std::exchange( other.capacity_, this->capacity_ );
        return *this;
    }
    // tr_vector's explicit operator= declarations hide the base class
    // initializer_list overload; re-expose it.
    constexpr tr_vector & operator=( std::initializer_list<value_type> const data ) { this->assign( data ); return *this; }
    constexpr ~tr_vector() noexcept
    {
        // for non trivial types have/generate one check for both the destroy
        // loop and call to free
        if ( std::is_trivially_destructible_v<T> || p_array_ )
        {
            std::destroy_n( data(), size() );
            storage_free();
        }
    }

    [[ nodiscard, gnu::pure ]] size_type size    () const noexcept { return size_; }
    [[ nodiscard, gnu::pure ]] size_type capacity() const noexcept
    {
        if constexpr ( options.cache_capacity )
        {
            BOOST_ASSUME( capacity_ >= size_ );
            BOOST_ASSERT( this->empty() || ( capacity_ <= al::size( p_array_ ) ) );
            return capacity_;
        }
        else
        {
            return this->empty() ? 0 : al::size( p_array_ );
        }
    }

    [[ nodiscard, gnu::pure, gnu::assume_aligned( alignment ) ]] value_type       * data()       noexcept { return std::assume_aligned<alignment>( p_array_ ); }
    [[ nodiscard, gnu::pure, gnu::assume_aligned( alignment ) ]] value_type const * data() const noexcept { return std::assume_aligned<alignment>( p_array_ ); }

    void reserve( size_type const new_capacity )
    {
        auto const quick_comparison_target{ options.cache_capacity ? capacity() : size() };
        if ( new_capacity > quick_comparison_target ) {
            p_array_ = al::grow_to( p_array_, quick_comparison_target, new_capacity );
            update_capacity( new_capacity );
        }
    }

    static constexpr al get_allocator() noexcept { return {}; }

    auto release() noexcept { auto data{ p_array_ }; mark_freed(); return data; }

private: friend base;
    [[ using gnu: cold, assume_aligned( alignment ) ]]
#ifdef _MSC_VER
    __declspec( noalias )
#endif
    constexpr value_type * storage_init( size_type const initial_size )
    {
        if ( initial_size )
        {
            p_array_ = al::allocate( initial_size );
            size_    = initial_size;
            update_capacity( initial_size );
        }
        else
        {
            mark_freed();
        }
        return data();
    }
    [[ using gnu: assume_aligned( alignment ), returns_nonnull ]]
    constexpr value_type * storage_grow_to( size_type const target_size )
    {
        auto const current_capacity{ capacity() };
        BOOST_ASSUME( current_capacity >= size_ );
        BOOST_ASSUME( target_size      >= size_ ); // allow no/zero change
        if ( target_size > current_capacity ) [[ unlikely ]] {
            do_grow( target_size, current_capacity );
        }
        size_ = target_size;
        return data();
    }

    [[ using gnu: assume_aligned( alignment ), returns_nonnull, cold ]]
    constexpr value_type * storage_shrink_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= size_ );
        p_array_ = al::shrink_to( p_array_, size_, target_size );
        BOOST_ASSUME( p_array_ || !target_size ); // assuming downsizing never fails
        BOOST_ASSUME( is_aligned( p_array_, alignment ) ); // assuming no implementation will actually move a block upon downsizing
        storage_shrink_size_to( target_size );
        update_capacity( target_size );
        return data();
    }
    constexpr void storage_shrink_size_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( size_ >= target_size );
        size_ = target_size;
    }
    void storage_dec_size() noexcept { BOOST_ASSUME( size_ >= 1 ); --size_; }
    void storage_inc_size() noexcept { BOOST_ASSUME( size_ < capacity() ); ++size_; }

#ifdef _MSC_VER
    //! Attempts in-place capacity expansion via ::_expand().
    //! Returns true if capacity() >= target_capacity after the call.
    bool storage_try_expand_capacity( size_type const target_capacity ) noexcept
    requires( options.cache_capacity && alignment <= detail::guaranteed_alignment )
    {
        namespace bc = boost::container;
        auto recv_size{ target_capacity };
        auto reuse    { p_array_ };
        auto const result{ al::allocation_command(
            bc::expand_fwd | bc::nothrow_allocation,
            target_capacity, recv_size, reuse
        ) };
        if ( result )
        {
            update_capacity( recv_size );
            return true;
        }
        return false;
    }
#endif

    void storage_free() noexcept
    {
        al::deallocate( data(), options.cache_capacity ? capacity() : 0 );
        mark_freed();
    }

private:
    [[ gnu::cold, gnu::noinline, clang::preserve_most ]]
    void do_grow( size_type const target_size, size_type const cached_current_capacity )
    {
        BOOST_ASSUME( cached_current_capacity == capacity() );
        auto const new_capacity
        {
            options.explicit_geometric_growth
                ? std::max( target_size, cached_current_capacity * 3U / 2U )
                : target_size
        };
        p_array_ = al::grow_to( p_array_, cached_current_capacity, new_capacity );
        update_capacity( new_capacity );
    }

    void update_capacity( [[ maybe_unused ]] size_type const requested_capacity ) noexcept
    {
        BOOST_ASSUME( p_array_ || !requested_capacity );
        if constexpr ( options.cache_capacity ) {
#       if defined( _MSC_VER )
            BOOST_ASSERT( !requested_capacity || ( al::size( p_array_ ) == requested_capacity ) ); // see note in crt_alloc_size
            capacity_ = requested_capacity;
#       else
            capacity_ = al::size( p_array_ );
            BOOST_ASSUME( capacity_ >= requested_capacity );
#       endif
        }
    }

    void mark_freed() noexcept { std::memset( static_cast<void *>( this ), 0, sizeof( *this ) ); }

private:
    // see the note for the default constructor
    T * __restrict p_array_;
    size_type      size_;
#ifdef _MSC_VER
    [[ msvc::no_unique_address ]]
#else
    [[ no_unique_address ]]
#endif
    std::conditional_t<options.cache_capacity, sz_t, decltype( std::ignore )> capacity_;
}; // struct tr_vector

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
