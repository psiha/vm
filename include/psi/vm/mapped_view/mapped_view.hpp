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
//------------------------------------------------------------------------------
#ifndef mapped_view_hpp__D9C84FF5_E506_4ECB_9778_61E036048D28
#define mapped_view_hpp__D9C84FF5_E506_4ECB_9778_61E036048D28
#pragma once
//------------------------------------------------------------------------------
#include "psi/vm/detail/impl_selection.hpp"
#include "psi/vm/error/error.hpp"
#include "psi/vm/handles/handle.hpp"
#include "psi/vm/mapping/mapping.hpp"

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

template <typename Element>
using basic_memory_range = std::span<Element>;

using           memory_range = basic_memory_range<std::byte      >;
using read_only_memory_range = basic_memory_range<std::byte const>;

template <typename Element, typename Mapper = PSI_VM_IMPL()::mapper>
class basic_mapped_view;

namespace detail0
{
    template <typename Element>
    memory_range
#ifdef BOOST_NO_STRICT_ALIASING
    const &
#endif // BOOST_NO_STRICT_ALIASING
    make_basic_view( basic_memory_range<Element> const & range )
    {
        return
#       ifdef BOOST_NO_STRICT_ALIASING
            reinterpret_cast<memory_range const &>( range );
#       else // compiler might care about strict aliasing rules
            {
                static_cast<std::byte *>( const_cast<void *>( static_cast<void const *>( range.data() ) ) ),
                range.size() * sizeof( memory_range::value_type ) / sizeof( Element )
            };
#       endif // BOOST_NO_STRICT_ALIASING
    }

    template <typename Element>
    basic_memory_range<Element>
#ifdef BOOST_NO_STRICT_ALIASING
    const &
#endif // BOOST_NO_STRICT_ALIASING
    make_typed_view( memory_range const & range )
    {
        //...zzz...add proper error handling...
        BOOST_ASSERT( reinterpret_cast<std::size_t>( range.data() ) % sizeof( Element ) == 0 );
        BOOST_ASSERT(                                range.size()   % sizeof( Element ) == 0 );
        return
#       ifdef BOOST_NO_STRICT_ALIASING
            reinterpret_cast<basic_memory_range<Element> const &>( range );
#       else // compiler might care about strict aliasing rules
            {
                static_cast<Element *>( static_cast<void *>( range.data() ) ),
                range.size() * sizeof( memory_range::value_type ) / sizeof( Element )
            };
#       endif // BOOST_NO_STRICT_ALIASING
    }
} // namespace detail0


////////////////////////////////////////////////////////////////////////////////
///
/// \class basic_mapped_view
///
/// \brief RAII wrapper around an immutable span that 'points to' a mapped
/// memory region.
///
////////////////////////////////////////////////////////////////////////////////

template <typename Element, typename Mapper /*= PSI_VM_IMPL()::mapper*/>
class basic_mapped_view : public basic_memory_range<Element>
{
public:
    using mapper         = Mapper;
    using error_t        = typename mapper::error_t  ;
    using flags_t        = typename mapper::flags_t  ;
    using mapping_t      = typename mapper::mapping_t;
    using memory_range_t = basic_memory_range<Element>;

public:
    constexpr basic_mapped_view() noexcept = default;
    basic_mapped_view
    (
        mapping_t     const & source_mapping,
        std::uint64_t         offset       = 0,
        std::size_t           desired_size = 0
    ) : basic_mapped_view{ map( source_mapping, offset, desired_size ) } {}
     basic_mapped_view( basic_mapped_view && other ) noexcept : memory_range_t{ other } { static_cast<memory_range_t &>( other ) = memory_range_t(); }
     basic_mapped_view( basic_mapped_view const &  ) = delete;
    ~basic_mapped_view(                            ) noexcept                           { do_unmap(); }

    basic_mapped_view & operator=( basic_mapped_view && other ) noexcept
    {
        do_unmap();
        static_cast<memory_range_t &>( *this ) = other;
        static_cast<memory_range_t &>( other ) = memory_range_t{};
        return *this;
    }

    bool flush() const { return mapper::flush( detail0::make_basic_view( *this ) ); }

    explicit operator bool() const noexcept { /*BOOST_ASSUME( memory_range_t::empty() == !memory_range_t::begin() );*/ return !memory_range_t::empty(); }

    void BOOST_CC_REG unmap() noexcept
    {
        do_unmap();
        static_cast<memory_range_t &>( *this ) = {};
    } 

    memory_range_t release() noexcept
    {
        memory_range_t span{ *this };
        static_cast<memory_range_t &>( *this ) = memory_range_t{};
        return span;
    }

public: // Factory methods.
    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
    err::fallible_result<basic_mapped_view, error_t> BOOST_CC_REG
    map
    (
        mapping       const &       source_mapping,
        std::uint64_t         const offset       = 0,
        std::size_t           const desired_size = 0
    ) noexcept
    {
        return map( source_mapping, source_mapping.view_mapping_flags, offset, desired_size );
    }

    //static memory_range_t BOOST_CC_REG map( T ... args ) noexcept { return make_typed_view( mapper::map( args ) ); }

    //static void BOOST_CC_REG unmap( memory_range_t const & view ) noexcept { mapper::unmap( make_basic_view( view ) ); }

    static BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
    err::fallible_result<basic_mapped_view, error_t> BOOST_CC_REG
    map
    (
        mapping::reference /*const*/ source_mapping,
        flags::viewing     const flags,
        std::uint64_t      const offset       = 0,
        std::size_t        const desired_size = 0
    ) noexcept
    {
        BOOST_ASSERT_MSG
        (
            !std::is_const<Element>::value || source_mapping.is_read_only(),
            "Use const element mapped view for read only mappings."
        );
        BOOST_ASSERT_MSG
        (
            flags <= source_mapping.view_mapping_flags,
            "Requested mapped view access level is more lax than that of the source mapping."
        );
        auto const mapped_memory_range{ mapper::map( source_mapping, flags, offset, desired_size ) };
        if ( BOOST_UNLIKELY( mapped_memory_range.empty() ) )
            return error_t{};
        return detail0::make_typed_view<Element>( mapped_memory_range );
    }

private:
    /// \note Required to enable the emplacement constructors of boost::err
    /// wrappers. To be solved in a cleaner way...
    ///                                       (28.05.2015.) (Domagoj Saric)
    friend class  err::result_or_error<basic_mapped_view, error_t>;
    friend struct std::is_constructible<basic_mapped_view, memory_range_t &&>;
    friend struct std::is_constructible<basic_mapped_view, memory_range_t const &>;
    basic_mapped_view( memory_range_t const & mapped_range            ) noexcept : memory_range_t{ mapped_range   } {}
    basic_mapped_view( Element * const p_begin, Element * const p_end ) noexcept : memory_range_t{ p_begin, p_end } {}

    BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
    void do_unmap() noexcept { mapper::unmap( detail0::make_basic_view( *this ) ); }
}; // class basic_mapped_view

using mapped_view           = basic_mapped_view<std::byte      >;
using read_only_mapped_view = basic_mapped_view<std::byte const>;

//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------

/// \note The related in-class friend declarations seem good enough for MSVC14
/// but not for Clang 3.6.
///                                           (04.06.2015.) (Domagoj Saric)
namespace std
{
    template <typename Element, typename Impl> struct is_constructible        <boost::mmap::basic_mapped_view<Element, Impl>, std::span<Element> &&> : std::true_type {};
    template <typename Element, typename Impl> struct is_nothrow_constructible<boost::mmap::basic_mapped_view<Element, Impl>, std::span<Element> &&> : std::true_type {};
} // namespace std

#ifdef PSI_VM_HEADER_ONLY
    #include "mapped_view.inl"
#endif

#endif // mapped_view_hpp
