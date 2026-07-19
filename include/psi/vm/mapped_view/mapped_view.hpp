////////////////////////////////////////////////////////////////////////////////
///
/// \file mapped_view.hpp
/// ---------------------
///
/// Copyright (c) Domagoj Saric 2010 - 2026.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
/// (See accompanying file LICENSE_1_0.txt or copy at
/// http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <psi/vm/allocation.hpp>
#include <psi/vm/detail/impl_selection.hpp>
#include <psi/vm/error/error.hpp>
#include <psi/vm/mapping/mapping.hpp>
#include <psi/vm/span.hpp>

#include <boost/assert.hpp>
#include <boost/config.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
///
/// \class basic_mapped_view
///
/// \brief RAII wrapper around an immutable span that 'points to' a mapped
/// memory region.
///
////////////////////////////////////////////////////////////////////////////////

template <bool read_only>
class [[ clang::trivial_abi ]] basic_mapped_view : public std::conditional_t<read_only, read_only_mapped_span, mapped_span>
{
public:
    using error_t    = error;
    using mapping_t  = mapping;
    using span       = std::conditional_t<read_only, read_only_mapped_span, mapped_span>;
    using value_type = typename span::value_type;

public:
    constexpr basic_mapped_view() noexcept = default;
    explicit basic_mapped_view
    (
        mapping_t     &       source_mapping,
        std::uint64_t   const offset       = 0,
        std::size_t     const desired_size = 0
    ) : basic_mapped_view{ map( source_mapping, offset, desired_size ) } {}
    basic_mapped_view( basic_mapped_view && other ) noexcept : span{ other } { static_cast<span &>( other ) = {}; }
    basic_mapped_view( basic_mapped_view const &  ) = delete;
   ~basic_mapped_view(                            ) noexcept { do_unmap(); }

    basic_mapped_view & operator=( basic_mapped_view && other ) noexcept
    {
        do_unmap();
        static_cast<span &>( *this ) = other;
        static_cast<span &>( other ) = {};
        return *this;
    }

    void shrink( std::size_t target_size ) noexcept;

    fallible_result<void> expand( std::size_t target_size, mapping & ) noexcept;

    explicit operator bool() const noexcept { return !this->empty(); }

    void unmap() noexcept
    {
        do_unmap();
        static_cast<span &>( *this ) = {};
    }

    span release() noexcept
    {
        span as_span{ *this };
        static_cast<span &>( *this ) = {};
        return as_span;
    }

    void swap( basic_mapped_view & other ) noexcept
    {
        std::swap( *this, other );
    }

public: // Factory methods.
    static err::fallible_result<basic_mapped_view, error_t>
    map
    (
        mapping       &       source_mapping,
        std::uint64_t   const offset       = 0,
        std::size_t     const desired_size = 0
    ) noexcept
    {
        return map( source_mapping, source_mapping.view_mapping_flags, offset, desired_size );
    }

    static err::fallible_result<basic_mapped_view, error_t>
    map
    (
        mapping        &       source_mapping,
        flags::viewing   const flags,
        std::uint64_t    const offset       = 0,
        std::size_t      const desired_size = 0
    ) noexcept;

    static auto map( mapping && source_mapping, auto const... args ) noexcept
    {
        return map( source_mapping, args... );
    }

private:
    /// \note Required to enable the emplacement constructors of err
    /// wrappers. To be solved in a cleaner way...
    ///                                       (28.05.2015.) (Domagoj Saric)
    friend class  err::result_or_error<basic_mapped_view, error_t>;
    friend struct std::is_constructible<basic_mapped_view, span &&>;
    friend struct std::is_constructible<basic_mapped_view, span const &>;
    basic_mapped_view( span const & mapped_range                            ) noexcept : span{ mapped_range   } {}
    basic_mapped_view( value_type * const p_begin, value_type * const p_end ) noexcept : span{ p_begin, p_end } {}

    void do_unmap() noexcept;
}; // class basic_mapped_view

using           mapped_view = basic_mapped_view<false>;
using read_only_mapped_view = basic_mapped_view<true >;

////////////////////////////////////////////////////////////////////////////////
///
/// \class reserving_basic_mapped_view
///
/// \brief Address-stable growable view: reserves a caller-specified virtual
/// address range up front and maps/commits the mapping's pages into it as the
/// view grows. The base address never changes for the lifetime of the view so
/// pointers into the mapped range remain valid across expansions.
///
/// Expansion beyond the reservation is a clean failure (no silent fallback to
/// a relocating remap) - the caller chooses the reservation size and owns the
/// consequences of exhausting it. On 64-bit targets address space is plentiful
/// so generous reservations are the intended usage.
///
/// Windows: MEM_RESERVE_PLACEHOLDER reservation + NtMapViewOfSectionEx
///          (MEM_REPLACE_PLACEHOLDER) section views mapped into it.
/// POSIX:   PROT_NONE MAP_NORESERVE anonymous reservation + MAP_FIXED file
///          mappings over its sub-ranges.
///
////////////////////////////////////////////////////////////////////////////////

template <bool read_only>
class [[ clang::trivial_abi ]] reserving_basic_mapped_view : public std::conditional_t<read_only, read_only_mapped_span, mapped_span>
{
public:
    using error_t    = error;
    using mapping_t  = mapping;
    using span       = std::conditional_t<read_only, read_only_mapped_span, mapped_span>;
    using value_type = typename span::value_type;

public:
    constexpr reserving_basic_mapped_view() noexcept = default;

    reserving_basic_mapped_view( reserving_basic_mapped_view && other ) noexcept
        : span{ other }
    {
        static_cast<span &>( other ) = {};
        reservation_size_   = std::exchange( other.reservation_size_  , 0 );
        kernel_mapped_size_ = std::exchange( other.kernel_mapped_size_, 0 );
    }

    reserving_basic_mapped_view( reserving_basic_mapped_view const & ) = delete;

   ~reserving_basic_mapped_view() noexcept { do_unmap(); }

    reserving_basic_mapped_view & operator=( reserving_basic_mapped_view && other ) noexcept
    {
        do_unmap();
        static_cast<span &>( *this ) = other;
        static_cast<span &>( other ) = {};
        reservation_size_   = std::exchange( other.reservation_size_  , 0 );
        kernel_mapped_size_ = std::exchange( other.kernel_mapped_size_, 0 );
        return *this;
    }

    //! Grow the view in place, within the reservation, mapping additional
    //! mapping pages at [current end, target_size). The base address is
    //! guaranteed unchanged on success. Fails (without side effects on the
    //! existing range) if target_size exceeds the reservation.
    fallible_result<void> expand( std::size_t target_size, mapping & ) noexcept;

    //! Shrink the (logical) view. Pages stay mapped (the high-water mapped
    //! extent is retained for cheap re-expansion); the base address is
    //! unchanged.
    void shrink( std::size_t const target_size ) noexcept
    {
        BOOST_ASSERT( target_size <= this->size() );
        static_cast<span &>( *this ) = { this->data(), target_size };
    }

    [[ nodiscard ]] std::size_t reservation_size() const noexcept { return reservation_size_; }

    explicit operator bool() const noexcept { return reservation_size_ != 0; }

    void unmap() noexcept
    {
        do_unmap();
        static_cast<span &>( *this ) = {};
        reservation_size_   = 0;
        kernel_mapped_size_ = 0;
    }

    void swap( reserving_basic_mapped_view & other ) noexcept { std::swap( *this, other ); }

public: // Factory methods.
    //! Reserve reservation_size bytes of address space and map the first
    //! desired_size bytes of the mapping into its beginning. desired_size may
    //! be zero - the whole range then remains a pure reservation until the
    //! first expand(). The view always covers the mapping from offset zero
    //! (expansion appends mapping pages at the current end).
    static err::fallible_result<reserving_basic_mapped_view, error_t>
    map
    (
        mapping       &       source_mapping,
        std::size_t     const reservation_size,
        std::size_t     const desired_size = 0
    ) noexcept;

private:
    friend class err::result_or_error<reserving_basic_mapped_view, error_t>;

    reserving_basic_mapped_view( span const & mapped_range, std::size_t const reservation_size, std::size_t const kernel_mapped_size ) noexcept
        : span{ mapped_range }, reservation_size_{ reservation_size }, kernel_mapped_size_{ kernel_mapped_size } {}

    [[ nodiscard ]] std::byte * base() const noexcept { return const_cast<std::byte *>( reinterpret_cast<std::byte const *>( this->data() ) ); }

    void do_unmap() noexcept;

private:
    std::size_t reservation_size_  { 0 }; //!< total reserved address range (aligned up to reserve_granularity)
    std::size_t kernel_mapped_size_{ 0 }; //!< high-water extent actually backed by mapping pages
}; // class reserving_basic_mapped_view

using           reserving_mapped_view = reserving_basic_mapped_view<false>;
using read_only_reserving_mapped_view = reserving_basic_mapped_view<true >;

#ifdef _WIN32
template <bool read_only>
class [[ clang::trivial_abi ]] extendable_basic_mapped_view : public basic_mapped_view<read_only>
{
public:
    using base       = basic_mapped_view<read_only>;
    using error_t    = typename base::error_t;
    using mapping_t  = typename base::mapping_t;
    using span       = typename base::span;
    using value_type = typename base::value_type;

public:
    constexpr extendable_basic_mapped_view() noexcept = default;
    explicit extendable_basic_mapped_view
    (
        mapping_t     &       source_mapping,
        std::uint64_t   const offset       = 0,
        std::size_t     const desired_size = 0
    ) : extendable_basic_mapped_view{ map( source_mapping, offset, desired_size ) } {}

    extendable_basic_mapped_view( extendable_basic_mapped_view && other ) noexcept : base{}
    {
        static_cast<span &>( *this ) = static_cast<span const &>( other );
        static_cast<span &>( other ) = {};
        trailing_placeholder_size_ = std::exchange( other.trailing_placeholder_size_, 0 );
    }

    extendable_basic_mapped_view( extendable_basic_mapped_view const & ) = delete;

   ~extendable_basic_mapped_view() noexcept
    {
        do_unmap();
        static_cast<span &>( *this ) = {};
    }

    extendable_basic_mapped_view & operator=( extendable_basic_mapped_view && other ) noexcept
    {
        do_unmap();
        static_cast<span &>( *this ) = static_cast<span const &>( other );
        static_cast<span &>( other ) = {};
        trailing_placeholder_size_ = std::exchange( other.trailing_placeholder_size_, 0 );
        return *this;
    }

    void unmap() noexcept
    {
        do_unmap();
        static_cast<span &>( *this ) = {};
    }

    span release() noexcept
    {
        span as_span{ *this };
        static_cast<span &>( *this ) = {};
        trailing_placeholder_size_ = 0;
        return as_span;
    }

    void shrink( std::size_t const target_size ) noexcept { base::shrink( target_size ); }

    fallible_result<void> expand( std::size_t target_size, mapping & source_mapping ) noexcept;

    explicit operator bool() const noexcept { return !this->empty(); }

    void swap( extendable_basic_mapped_view & other ) noexcept
    {
        std::swap( *this, other );
    }

public: // Factory methods.
    static err::fallible_result<extendable_basic_mapped_view, error_t>
    map
    (
        mapping       &       source_mapping,
        std::uint64_t   const offset       = 0,
        std::size_t     const desired_size = 0
    ) noexcept
    {
        return map( source_mapping, source_mapping.view_mapping_flags, offset, desired_size );
    }

    static err::fallible_result<extendable_basic_mapped_view, error_t>
    map
    (
        mapping        &       source_mapping,
        flags::viewing   const flags,
        std::uint64_t    const offset       = 0,
        std::size_t      const desired_size = 0
    ) noexcept
    {
        auto base_view{ base::map( source_mapping, flags, offset, desired_size )() };
        if ( !base_view ) [[ unlikely ]]
            return base_view.error();

        extendable_basic_mapped_view view;
        static_cast<span &>( view ) = static_cast<span const &>( *base_view );
        static_cast<span &>( *base_view ) = {};
        return view;
    }

    static auto map( mapping && source_mapping, auto const... args ) noexcept
    {
        return map( source_mapping, args... );
    }

private:
    void do_unmap() noexcept;

private:
    std::size_t trailing_placeholder_size_{ 0 };
};

using extendable_mapped_view = extendable_basic_mapped_view<false>;
#else
template <bool read_only>
using extendable_basic_mapped_view = basic_mapped_view<read_only>;

using extendable_mapped_view = mapped_view;
#endif

//------------------------------------------------------------------------------
} // namespace vm::psi
//------------------------------------------------------------------------------
