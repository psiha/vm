////////////////////////////////////////////////////////////////////////////////
///
/// \file mmap_threshold.hpp
/// ------------------------
///
/// mmap_threshold_storage: a vector storage that holds its elements in a
/// heap_storage until they grow to `threshold` bytes and in a vm_storage (a
/// mapping of their own) from there on: growth across the threshold moves
/// them into a mapping, which they then keep until the storage is freed -
/// shrinking releases pages of the mapping, it never moves the elements back
/// to the heap.
///
/// Why, when general purpose allocators already do this internally (glibc
/// malloc serves blocks of M_MMAP_THRESHOLD bytes or more - 128 KiB at first,
/// raised dynamically up to 32 MiB - with mmap; mimalloc v3 serves blocks
/// above 32 MiB straight from the OS)? Only for what those internals do not
/// guarantee, which depends on the allocator:
///
/// - Growth without a copy. vm_storage grows by remapping (mremap on Linux,
///   placeholders on Windows), moving pages rather than bytes. glibc's
///   realloc does that too for a block it mmapped; mimalloc's realloc never
///   does (v3.2.8: a growth past the block's usable size is always a new
///   allocation, a memcpy and a free, whatever the size).
/// - Release that is immediate and deterministic. Shrinking or freeing a
///   vm_storage returns its pages to the OS in the call. mimalloc keeps a
///   freed block of up to 32 MiB in its arena and decommits it only after a
///   delay (the purge_delay option times arena_purge_mult: 1 s by default in
///   v3.2.8), which can be set to 0 - for the whole process, at the cost of
///   decommitting and recommitting on every free/allocate cycle there. glibc
///   returns an mmapped block on free, but its threshold rises to the size of
///   such a block once it is freed, after which blocks up to that size stay
///   in the heap.
///
/// What it costs: below the threshold nothing over heap_storage (a mapping
/// per small container would cost a page or more each, a kernel mapping
/// entry and a system call per growth); one copy of the elements at the
/// switch; a vm_storage is also bigger than a heap_storage, so the storage
/// object is too. The hot path (data(), size(), capacity(),
/// appending within capacity) reads three members of this class and never
/// asks which storage is active.
///
/// Requires a trivially moveable T (as vm_storage does): the elements are
/// moved into the mapping bitwise. The storage itself is trivially moveable
/// (declared, see is_trivially_moveable below), so a container of these
/// relocates them by memcpy/realloc.
///
/// Copyright (c) Domagoj Saric 2026.
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

#include <psi/vm/allocators/allocator_base.hpp> // detail::throw_bad_alloc
#include <psi/vm/containers/growth_policy.hpp>
#include <psi/vm/containers/is_trivially_moveable.hpp>
#include <psi/vm/containers/storage/heap.hpp>
#include <psi/vm/containers/storage/vm.hpp>
#include <psi/vm/containers/vector.hpp>

#include <boost/assert.hpp>

#include <cstddef>
#include <cstring>
#include <tuple> // ignore
#include <type_traits>
#include <utility>
#include <variant>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

template <typename T, typename sz_t = std::size_t, std::size_t threshold = 128 * 1024>
requires is_trivially_moveable<T>
class [[ nodiscard ]] mmap_threshold_storage
{
public:
    using value_type = T;
    using size_type  = sz_t;

    using small_storage = heap_storage<T, sz_t>;
    using large_storage = vm_storage  <T, sz_t>;

    static bool constexpr storage_zero_initialized{ false };
    // Opt-in (a class with its own move operations is not deduced to be): neither
    // alternative nor the cache holds a pointer to the object itself -
    // heap_storage is a pointer and sizes, vm_storage (trivial_abi) handles
    // and a view - so the object may be relocated bitwise.
    static bool constexpr is_trivially_moveable   { true  };
    static bool constexpr fixed_sized_copy        { false };

    static bool constexpr length_error_is_reportable{ small_storage::length_error_is_reportable };
    [[ nodiscard ]] static constexpr size_type max_size() noexcept { return small_storage::max_size(); }

    //! Whether growth to `capacity` elements takes a heap block into a mapping.
    [[ nodiscard, gnu::const ]] static constexpr bool mapped( size_type const capacity ) noexcept { return std::size_t{ capacity } * sizeof( T ) >= threshold; }

    constexpr mmap_threshold_storage() noexcept = default;

    mmap_threshold_storage( mmap_threshold_storage const & ) = delete; // element copies are vector<>'s
    mmap_threshold_storage & operator=( mmap_threshold_storage const & ) = delete;

    mmap_threshold_storage( mmap_threshold_storage && other ) noexcept
        : data_{ other.data_ }, size_{ other.size_ }, capacity_{ other.capacity_ }, storage_{ std::move( other.storage_ ) }
    { other.storage_.template emplace<small_storage>(); other.refresh(); }

    mmap_threshold_storage & operator=( mmap_threshold_storage && other ) noexcept
    {
        if ( this != &other )
        {
            other.sync();
            storage_ = std::move( other.storage_ );
            other.storage_.template emplace<small_storage>();
            other.refresh();
            refresh();
        }
        return *this;
    }

    void swap( mmap_threshold_storage & other ) noexcept
    {
        sync(); other.sync();
        std::swap( storage_, other.storage_ );
        refresh(); other.refresh();
    }

    // --- the hot path: cached, the same whichever storage is active ---
    [[ nodiscard, gnu::pure ]] T       * data()       noexcept { return data_; }
    [[ nodiscard, gnu::pure ]] T const * data() const noexcept { return data_; }
    [[ nodiscard, gnu::pure ]] size_type size    () const noexcept { return size_; }
    [[ nodiscard, gnu::pure ]] size_type capacity() const noexcept { return capacity_; }
    [[ nodiscard, gnu::pure ]] bool      empty   () const noexcept { return !size_; }

    //! Whether the elements are in a mapping (vm_storage) at the moment.
    [[ nodiscard, gnu::pure ]] bool is_mapped() const noexcept { return std::holds_alternative<large_storage>( storage_ ); }

    void storage_shrink_size_to( size_type const target_size ) noexcept { BOOST_ASSUME( target_size <= size_ ); size_ = target_size; }
    void storage_dec_size() noexcept { BOOST_ASSUME( size_ >= 1         ); --size_; }
    void storage_inc_size() noexcept { BOOST_ASSUME( size_ <  capacity_ ); ++size_; }

    // --- cold paths ---
    void reserve( size_type const new_capacity )
    {
        if ( new_capacity > capacity_ )
            grow_capacity_to( new_capacity );
    }

    T * storage_init( size_type const initial_size )
    {
        BOOST_ASSERT( !data_ );
        if ( initial_size )
        {
            grow_capacity_to( initial_size );
            size_ = initial_size;
        }
        return data_;
    }

    template <geometric_growth G = geometric_growth{ 1, 1 }>
    T * storage_grow_to( size_type const target_size )
    {
        BOOST_ASSUME( target_size >= size_ );
        if ( target_size > capacity_ ) [[ unlikely ]]
            grow_capacity_to( G ? G( target_size, capacity_, max_size() ) : target_size );
        size_ = target_size;
        return data_;
    }

    //! Releases the capacity past target_size, in the active storage: a
    //! mapping stays a mapping, at any size, until the storage is freed.
    PSI_COLD
    T * storage_shrink_to( size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= size_ );
        size_ = target_size;
        sync();
        if ( auto * const large{ std::get_if<large_storage>( &storage_ ) } )
            large->storage_shrink_to_fit();
        else
            std::get<small_storage>( storage_ ).storage_shrink_to( target_size );
        refresh();
        return data_;
    }
    void storage_shrink_to_fit() noexcept { storage_shrink_to( size_ ); }

    void storage_free() noexcept
    {
        storage_.template emplace<small_storage>();
        refresh();
    }

private:
    // The active storage learns the size the cache holds (only the cache is
    // written on the hot path) - always within its capacity.
    void sync() noexcept
    {
        std::visit( [ this ]( auto & storage ) noexcept {
            if ( storage.size() > size_ )
                storage.storage_shrink_size_to( size_ );
            else if ( storage.size() < size_ )
                std::ignore = storage.storage_grow_to( size_ ); // within capacity: no allocation
        }, storage_ );
    }

    void refresh() noexcept
    {
        std::visit( [ this ]( auto & storage ) noexcept {
            data_     = storage.data    ();
            size_     = storage.size    ();
            capacity_ = storage.capacity();
        }, storage_ );
    }

    // The one decision point: a mapping grows as a mapping; a heap block
    // grows into one once its new capacity reaches the threshold.
    PSI_COLD [[ gnu::noinline ]]
    void grow_capacity_to( size_type const new_capacity )
    {
        BOOST_ASSUME( new_capacity > capacity_ );
        auto const live_size{ size_ };
        sync();
        if ( auto * const large{ std::get_if<large_storage>( &storage_ ) } )
        {
            large->reserve( new_capacity );
        }
        else if ( !mapped( new_capacity ) )
        {
            std::get<small_storage>( storage_ ).reserve( new_capacity );
        }
        else
        {
            // into a mapping of the whole new capacity, in one call
            large_storage mapping;
            if ( !static_cast<bool>( mapping.map_memory( new_capacity, {}, no_init ) ) ) [[ unlikely ]]
                detail::throw_bad_alloc();
            mapping.storage_shrink_size_to( live_size );
            if ( live_size )
                std::memcpy( static_cast<void *>( mapping.data() ), data_, std::size_t{ live_size } * sizeof( T ) );
            storage_ = std::move( mapping ); // frees the heap block
        }
        refresh();
        BOOST_ASSUME( size_ == live_size );
    }

    // the cache: what the hot path reads
    T *       data_    { nullptr };
    size_type size_    { 0 };
    size_type capacity_{ 0 };
    std::variant<small_storage, large_storage> storage_;
}; // class mmap_threshold_storage


static_assert( is_trivially_moveable<mmap_threshold_storage<int>> );
static_assert( is_trivially_moveable<vector<mmap_threshold_storage<int>>> );

template <typename T, typename sz_t = std::size_t, std::size_t threshold = 128 * 1024, geometric_growth growth = {}>
using mmap_threshold_vector = vector<mmap_threshold_storage<T, sz_t, threshold>, growth>;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
