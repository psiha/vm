////////////////////////////////////////////////////////////////////////////////
///
/// \file mapped_view.hpp
/// ---------------------
///
/// Copyright (c) Domagoj Saric 2010 - 2015.
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
#include "boost/mmap/detail/impl_selection.hpp"
#include "boost/mmap/error/error.hpp"
#include "boost/mmap/handles/handle.hpp"
#include "boost/mmap/mapping/mapping.hpp"

#include "boost/assert.hpp"
#include "boost/config.hpp"
#include "boost/noncopyable.hpp"
#include "boost/range/iterator_range_core.hpp"

#include <cstdint>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

inline namespace BOOST_MMAP_IMPL() { struct mapper; }

template <typename Element>
using basic_memory_range = iterator_range<Element * /*const...mrmlj...iterator_traits fail*/>;

using           memory_range = basic_memory_range<char      >;
using read_only_memory_range = basic_memory_range<char const>;

template <typename Element, typename Mapper = BOOST_MMAP_IMPL()::mapper>
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
        #ifdef BOOST_NO_STRICT_ALIASING
            reinterpret_cast<memory_range const &>( range );
        #else // compiler might care about strict aliasing rules
            {
                static_cast<char *>( const_cast<void *>( static_cast<void const *>( range.begin() ) ) ),
                static_cast<char *>( const_cast<void *>( static_cast<void const *>( range.end  () ) ) )
            };
        #endif // BOOST_NO_STRICT_ALIASING
    }

    template <typename Element>
    basic_memory_range<Element>
#ifdef BOOST_NO_STRICT_ALIASING
    const &
#endif // BOOST_NO_STRICT_ALIASING
    make_typed_view( memory_range const & range )
    {
        //...zzz...add proper error handling...
        BOOST_ASSERT( reinterpret_cast<std::size_t>( range.begin() ) % sizeof( Element ) == 0 );
        BOOST_ASSERT( reinterpret_cast<std::size_t>( range.end  () ) % sizeof( Element ) == 0 );
        BOOST_ASSERT(                                range.size ()   % sizeof( Element ) == 0 );
        return
        #ifdef BOOST_NO_STRICT_ALIASING
            reinterpret_cast<basic_memory_range<Element> const &>( range );
        #else // compiler might care about strict aliasing rules
            {
                static_cast<Element *>( static_cast<void *>( range.begin() ) ),
                static_cast<Element *>( static_cast<void *>( range.end  () ) )
            };
        #endif // BOOST_NO_STRICT_ALIASING
    }
} // namespace detail0


////////////////////////////////////////////////////////////////////////////////
///
/// \class basic_mapped_view
///
/// \brief RAII wrapper around an immutable boost::iterator_range that 'points
/// to' a mapped memory region.
///
////////////////////////////////////////////////////////////////////////////////

template <typename Element, typename Mapper /*= BOOST_MMAP_IMPL()::mapper*/>
class basic_mapped_view : public basic_memory_range<Element>
{
public:
    using mapper         = Mapper;
    using error_t        = typename mapper::error_t  ;
    using flags_t        = typename mapper::flags_t  ;
    using mapping_t      = typename mapper::mapping_t;
    using memory_range_t = basic_memory_range<Element>;

public:
  //basic_mapped_view() noexcept = default; // Clang 3.6 error : exception specification of explicitly defaulted default constructor does not match the calculated one
    basic_mapped_view() noexcept {}
    basic_mapped_view
    (
        mapping_t const & source_mapping,
        std::uint64_t         offset       = 0,
        std::size_t           desired_size = 0
    ) : basic_mapped_view( map( source_mapping, offset, desired_size ) ) {}
     basic_mapped_view( basic_mapped_view && other ) noexcept : memory_range_t( other ) { static_cast<memory_range_t &>( other ) = memory_range_t(); }
     basic_mapped_view( basic_mapped_view const &  ) = delete;
    ~basic_mapped_view(                      ) noexcept                           { do_unmap(); }

    basic_mapped_view & operator=( basic_mapped_view && other ) noexcept
    {
        do_unmap();
        static_cast<memory_range_t &>( *this ) = other;
        static_cast<memory_range_t &>( other ) = memory_range_t();
        return *this;
    }

    bool flush() const { return mapper::flush( detail0::make_basic_view( *this ) ); }

    explicit operator bool() const noexcept { /*BOOST_ASSUME( memory_range_t::empty() == !memory_range_t::begin() );*/ return memory_range_t::begin() != nullptr; }

    /// \note This one still gets called in certain contexts.?.investigate...
    ///                                       (28.05.2015.) (Domagoj Saric)
    operator typename memory_range_t::unspecified_bool_type() const noexcept { return this->operator bool(); }

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
        return detail0::make_typed_view<Element>( mapper::map( source_mapping, flags, offset, desired_size ) );
    }

private:
    void BOOST_CC_REG unmap() noexcept
    {
        do_unmap();
        static_cast<memory_range_t &>( *this ) = memory_range_t();
    }
    /// \note Required to enable the emplacement constructors of boost::err
    /// wrappers. To be solved in a cleaner way...
    ///                                       (28.05.2015.) (Domagoj Saric)
    friend class  err::result_or_error<basic_mapped_view, error_t>;
    friend struct std::is_constructible<basic_mapped_view, memory_range_t &&>;
    friend struct std::is_constructible<basic_mapped_view, memory_range_t const &>;
    basic_mapped_view( memory_range_t const & mapped_range            ) noexcept : memory_range_t( mapped_range   ) {}
    basic_mapped_view( Element * const p_begin, Element * const p_end ) noexcept : memory_range_t( p_begin, p_end ) {}

    BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_EXCEPTIONLESS )
    void do_unmap() noexcept { mapper::unmap( detail0::make_basic_view( *this ) ); }

private: // Hide mutable members
    using memory_range_t::advance_begin;
    using memory_range_t::advance_end  ;

    using memory_range_t::pop_front;
    using memory_range_t::pop_back ;
}; // class basic_mapped_view

using mapped_view           = basic_mapped_view<char      >;
using read_only_mapped_view = basic_mapped_view<char const>;

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

/// \note The related in-class friend declarations seem good enough for MSVC14
/// but not for Clang 3.6.
///                                           (04.06.2015.) (Domagoj Saric)
namespace std
{
    template <typename Element, typename Impl> struct is_constructible        <boost::mmap::basic_mapped_view<Element, Impl>, boost::iterator_range<Element *> &&> : std::true_type {};
    template <typename Element, typename Impl> struct is_nothrow_constructible<boost::mmap::basic_mapped_view<Element, Impl>, boost::iterator_range<Element *> &&> : std::true_type {};
} // namespace std

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "mapped_view.inl"
#endif

#endif // mapped_view_hpp
