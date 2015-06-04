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

template <typename Element>
using memory_range = iterator_range<Element * /* const*/>;

using basic_memory_range_t           = memory_range<char      >;
using basic_read_only_memory_range_t = memory_range<char const>;

template <typename Element, typename Impl = BOOST_MMAP_IMPL()>
class mapped_view;

namespace detail
{
    template <typename Element, typename Impl>
    struct mapper
    {
    public:
        using memory_range_t = memory_range<Element>;

        BOOST_ATTRIBUTES( BOOST_COLD, BOOST_EXCEPTIONLESS )
        static memory_range_t BOOST_CC_REG map
        (
            mapping<Impl> const & source_mapping,
            std::uint64_t         offset        ,
            std::size_t           desired_size
        ) noexcept
        {
            return make_typed_view( mapper<char, Impl>::map( source_mapping, offset, desired_size ) );
        }

        BOOST_ATTRIBUTES( BOOST_COLD, BOOST_EXCEPTIONLESS )
        static void BOOST_CC_REG unmap( memory_range_t const & view ) noexcept
        {
            mapper<char, Impl>::unmap( make_basic_view( view ) );
        }

    private:
        static basic_memory_range_t
    #ifdef BOOST_NO_STRICT_ALIASING
        const &
    #endif // BOOST_NO_STRICT_ALIASING
        make_basic_view( memory_range_t const & range )
        {
            return
            #ifdef BOOST_NO_STRICT_ALIASING
                reinterpret_cast<basic_memory_range_t const &>( range );
            #else // compiler might care about strict aliasing rules
                {
                    static_cast<char *>( const_cast<void *>( static_cast<void const *>( range.begin() ) ) ),
                    static_cast<char *>( const_cast<void *>( static_cast<void const *>( range.end  () ) ) )
                };
            #endif // BOOST_NO_STRICT_ALIASING
        }

        static memory_range_t
    #ifdef BOOST_NO_STRICT_ALIASING
        const &
    #endif // BOOST_NO_STRICT_ALIASING
        make_typed_view( basic_memory_range_t const & range )
        {
            //...zzz...add proper error handling...
            BOOST_ASSERT( reinterpret_cast<std::size_t>( range.begin() ) % sizeof( Element ) == 0 );
            BOOST_ASSERT( reinterpret_cast<std::size_t>( range.end  () ) % sizeof( Element ) == 0 );
            BOOST_ASSERT(                                range.size ()   % sizeof( Element ) == 0 );
            return
            #ifdef BOOST_NO_STRICT_ALIASING
                reinterpret_cast<memory_range_t const &>( range );
            #else // compiler might care about strict aliasing rules
                {
                    static_cast<Element *>( static_cast<void *>( range.begin() ) ),
                    static_cast<Element *>( static_cast<void *>( range.end  () ) )
                };
            #endif // BOOST_NO_STRICT_ALIASING
        }
    }; // struct mapper

    template <typename Impl>
    struct mapper<char, Impl>
    {
        static basic_memory_range_t map
        (
            mapping<Impl> const & source_mapping,
            std::uint64_t         offset        ,
            std::size_t           desired_size
        );

        static void unmap( basic_memory_range_t );
    }; // struct mapper<char, Impl>
} // namespace detail


////////////////////////////////////////////////////////////////////////////////
///
/// \class mapped_view
///
/// \brief RAII wrapper around an immutable boost::iterator_range that 'points
/// to' a mapped memory region.
///
////////////////////////////////////////////////////////////////////////////////

template <typename Element, typename Impl /*= BOOST_MMAP_IMPL()*/>
class mapped_view : public memory_range<Element>
{
public:
    using memory_range_t = memory_range<Element>;

public:
    mapped_view() noexcept {}// = default;
    mapped_view
    (
        mapping<Impl> const & source_mapping,
        std::uint64_t         offset       = 0,
        std::size_t           desired_size = 0
    ) : mapped_view( map( source_mapping, offset, desired_size ) ) {}
     mapped_view( mapped_view && other ) noexcept : memory_range_t( other ) { static_cast<memory_range_t &>( other ) = memory_range_t(); }
    ~mapped_view(                      ) noexcept                           { do_unmap(); }

    mapped_view & operator=( mapped_view && other ) noexcept
    {
        do_unmap();
        static_cast<memory_range_t &>( *this ) = other;
        static_cast<memory_range_t &>( other ) = memory_range_t();
        return *this;
    }

    explicit operator bool() const noexcept { BOOST_ASSUME( memory_range_t::empty() == !memory_range_t::begin() ); return memory_range_t::begin() != nullptr; }

    /// \note This one still gets called in certain contexts.?.investigate...
    ///                                       (28.05.2015.) (Domagoj Saric)
    operator typename memory_range_t::unspecified_bool_type() const noexcept { return this->operator bool(); }

public: // Factory methods.
    static
    err::fallible_result<mapped_view, mmap::error<>> BOOST_CC_REG
    map
    (
        mapping<Impl> const & source_mapping,
        std::uint64_t         offset       = 0,
        std::size_t           desired_size = 0
    ) noexcept
    {
        BOOST_ASSERT_MSG
        (
            !std::is_const<Element>::value || source_mapping.is_read_only(),
            "Use const element mapped view for read only mappings."
        );
        return detail::mapper<Element, Impl>::map( source_mapping, offset, desired_size );
    }

    void BOOST_CC_REG unmap() noexcept
    {
        do_unmap();
        static_cast<memory_range_t &>( *this ) = memory_range_t();
    }

private:
    /// \note Ideally we'd friend only the
    /// <typename AnyElement> struct detail::mapper<AnyElement, Impl>
    /// specialisation but the standard does not allow befriending partial
    /// specialisations.
    ///                                       (28.05.2015.) (Domagoj Saric)
    template <typename AnyElement, typename Impl_> friend struct detail::mapper;
    /// \note Required to enable the emplacement constructors of boost::err
    /// wrappers. To be solved in a cleaner way...
    ///                                       (28.05.2015.) (Domagoj Saric)
    friend class  err::result_or_error<mapped_view, mmap::error<>>;
    friend struct std::is_constructible<mapped_view, memory_range_t &&>;
    friend struct std::is_constructible<mapped_view, memory_range_t const &>;
    mapped_view( memory_range_t const & mapped_range            ) noexcept : memory_range_t( mapped_range   ) {}
    mapped_view( Element * const p_begin, Element * const p_end ) noexcept : memory_range_t( p_begin, p_end ) {}

    BOOST_ATTRIBUTES( BOOST_COLD, BOOST_EXCEPTIONLESS )
    void do_unmap() noexcept
    {
        detail::mapper<Element, Impl>::unmap( static_cast<memory_range_t &>( *this ) );
    }

private: // Hide mutable members
    using memory_range_t::advance_begin;
    using memory_range_t::advance_end  ;

    using memory_range_t::pop_front;
    using memory_range_t::pop_back ;
}; // class mapped_view

using basic_mapped_view           = mapped_view<char      >;
using basic_read_only_mapped_view = mapped_view<char const>;

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
    template <typename Element, typename Impl> struct is_constructible        <boost::mmap::mapped_view<Element, Impl>, boost::iterator_range<Element *> &&> : std::true_type {};
    template <typename Element, typename Impl> struct is_nothrow_constructible<boost::mmap::mapped_view<Element, Impl>, boost::iterator_range<Element *> &&> : std::true_type {};
} // namespace std

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "mapped_view.inl"
#endif

#endif // mapped_view_hpp
