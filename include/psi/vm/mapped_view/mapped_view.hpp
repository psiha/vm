////////////////////////////////////////////////////////////////////////////////
///
/// \file mapped_view.hpp
/// ---------------------
///
/// Copyright (c) Domagoj Saric 2010 - 2024.
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
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------

inline namespace PSI_VM_IMPL() { struct mapper; }

////////////////////////////////////////////////////////////////////////////////
///
/// \class basic_mapped_view
///
/// \brief RAII wrapper around an immutable span that 'points to' a mapped
/// memory region.
///
////////////////////////////////////////////////////////////////////////////////

template <bool read_only>
class basic_mapped_view : public std::conditional_t<read_only, read_only_mapped_span, mapped_span>
{
public:
    using mapper    = PSI_VM_IMPL()::mapper;
    using error_t   = error;
    using mapping_t = mapping;
    using span      = std::conditional_t<read_only, read_only_mapped_span, mapped_span>;
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
   ~basic_mapped_view(                            ) noexcept                 { do_unmap(); }

    basic_mapped_view & operator=( basic_mapped_view && other ) noexcept
    {
        do_unmap();
        static_cast<span &>( *this ) = other;
        static_cast<span &>( other ) = {};
        return *this;
    }

    void shrink( std::size_t target_size ) noexcept;

    fallible_result<void> expand( std::size_t target_size, mapping & ) noexcept;

    void flush() const noexcept requires( !read_only );

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

public: // Factory methods.
    static err::fallible_result<basic_mapped_view, error_t> BOOST_CC_REG
    map
    (
        mapping       &       source_mapping,
        std::uint64_t   const offset       = 0,
        std::size_t     const desired_size = 0
    ) noexcept
    {
        return map( source_mapping, source_mapping.view_mapping_flags, offset, desired_size );
    }

    static err::fallible_result<basic_mapped_view, error_t> BOOST_CC_REG
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

//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------
