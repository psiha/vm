////////////////////////////////////////////////////////////////////////////////
///
/// \file mapped_view.hpp
/// ---------------------
///
/// Copyright (c) Domagoj Saric 2010.-2011.
///
///  Use, modification and distribution is subject to the Boost Software License, Version 1.0.
///  (See accompanying file LICENSE_1_0.txt or copy at
///  http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef mapped_view_hpp__D9C84FF5_E506_4ECB_9778_61E036048D28
#define mapped_view_hpp__D9C84FF5_E506_4ECB_9778_61E036048D28
#pragma once
//------------------------------------------------------------------------------
#include "mappble_objects/posix_file/handle.hpp"
#include "mappble_objects/win32_file/handle.hpp"

#include "boost/assert.hpp"
#include "boost/noncopyable.hpp"
#include "boost/range/iterator_range.hpp"

#ifndef _WIN32
    #include "fcntl.h"
#endif // _WIN32

#ifdef _WIN32
    #define BOOST_AUX_IO_WIN32_OR_POSIX( win32, posix ) win32
#else
    #define BOOST_AUX_IO_WIN32_OR_POSIX( win32, posix ) posix
#endif
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

// Implementation note:
//   Using structs with public members and factory functions to enable (almost)
// zero-overhead 'link-time' conversion to native flag formats and to allow the
// user to modify the created flags or create fully custom ones so that specific
// platform-dependent use-cases, not otherwise covered through the generic
// interface, can also be covered.
//                                            (10.10.2010.) (Domagoj Saric)

struct mapping_flags
{
    struct handle_access_rights
    {
        static unsigned int const read   ;
        static unsigned int const write  ;
        static unsigned int const execute;
    };

    struct share_mode
    {
        static unsigned int const shared;
        static unsigned int const hidden;
    };

    struct system_hint
    {
        static unsigned int const strict_target_address  ;
        static unsigned int const lock_to_ram            ;
        static unsigned int const reserve_page_file_space;
        static unsigned int const precommit              ;
        static unsigned int const uninitialized          ;
    };

    static mapping_flags create
    (
        unsigned int handle_access_rights,
        unsigned int share_mode          ,
        unsigned int system_hints
    );

#ifdef _WIN32
    unsigned int create_mapping_flags;
    unsigned int map_view_flags;
#else
    int protection;
    int flags     ;
#endif // _WIN32
};


typedef iterator_range<unsigned char       *> basic_memory_range_t;
typedef iterator_range<unsigned char const *> basic_read_only_memory_range_t;

template <typename Element>
class mapped_view_reference;

typedef mapped_view_reference<unsigned char      > basic_mapped_view_ref;
typedef mapped_view_reference<unsigned char const> basic_mapped_read_only_view_ref;

namespace detail
{
    template <typename Element>
    class mapped_view_base : public iterator_range<Element *>
    {
    public:
        typedef iterator_range<Element> memory_range_t;

    public: // Factory methods.
        static void unmap( mapped_view_base const & );

    protected:
        mapped_view_base( iterator_range<Element *> const & mapped_range ) : iterator_range<Element *>( mapped_range   ) {}
        mapped_view_base( Element * const p_begin, Element * const p_end ) : iterator_range<Element *>( p_begin, p_end ) {}

        static mapped_view_reference<unsigned char const>
        #ifdef BOOST_MSVC
            const &
        #endif
        make_basic_view( mapped_view_base<Element> const & );

        static mapped_view_reference<Element>
        #ifdef BOOST_MSVC
            const &
        #endif
        make_typed_range( mapped_view_base<unsigned char> const & );

    private: // Hide mutable members
        void advance_begin();
        void advance_end  ();

        void pop_front();
        void pop_back ();
    };


    template <typename Element>
    void mapped_view_base<Element>::unmap( mapped_view_base<Element> const & mapped_range )
    {
        mapped_view_base<unsigned char const>::unmap( make_basic_view( mapped_range ) );
    }

    template <>
    void mapped_view_base<unsigned char const>::unmap( mapped_view_base<unsigned char const> const & );
} // namespace detail

template <typename Element>
class mapped_view_reference : public detail::mapped_view_base<Element>
{
public:
    basic_memory_range_t basic_range() const
    {
        return basic_memory_range_t
        (
            static_cast<unsigned char *>( static_cast<void *>( this->begin() ) ),
            static_cast<unsigned char *>( static_cast<void *>( this->end  () ) )
        );
    }

public: // Factory methods.
    static mapped_view_reference map
    (
        guard::native_handle_t,
        mapping_flags const &,
        std::size_t desired_size = 0,
        std::size_t offset       = 0
    );

private:
    template <typename T> friend class detail::mapped_view_base;

    mapped_view_reference( iterator_range<Element *> const & mapped_range ) : detail::mapped_view_base<Element>( mapped_range   ) {}
    mapped_view_reference( Element * const p_begin, Element * const p_end ) : detail::mapped_view_base<Element>( p_begin, p_end ) {}
};

template <typename Element>
class mapped_view_reference<Element const> : public detail::mapped_view_base<Element const>
{
public:
    basic_memory_range_t basic_range() const
    {
        return basic_memory_range_t
        (
            static_cast<unsigned char const *>( static_cast<void const *>( this->begin() ) ),
            static_cast<unsigned char const *>( static_cast<void const *>( this->end  () ) )
        );
    }

public: // Factory methods.
    static mapped_view_reference map
    (
        guard::native_handle_t object_handle,
        std::size_t            desired_size           = 0,
        std::size_t            offset                 = 0,
        bool                   map_for_code_execution = false
    );

private:
    template <typename T> friend class detail::mapped_view_base;

    mapped_view_reference( iterator_range<Element const *> const & mapped_range       ) : detail::mapped_view_base<Element const>( mapped_range   ) {}
    mapped_view_reference( Element const * const p_begin, Element const * const p_end ) : detail::mapped_view_base<Element const>( p_begin, p_end ) {}
    mapped_view_reference( mapped_view_reference<Element>  const & mutable_view       ) : detail::mapped_view_base<Element const>( mutable_view   ) {}
};


namespace detail
{
    // Implementation note:
    //   These have to be defined after mapped_view_reference for eager
    // compilers (e.g. GCC and Clang).
    //                                        (14.07.2011.) (Domagoj Saric)

    template <typename Element>
    mapped_view_reference<unsigned char const>
    #ifdef BOOST_MSVC
        const &
    #endif
    mapped_view_base<Element>::make_basic_view( mapped_view_base<Element> const & range )
    {
        return
        #ifdef BOOST_MSVC
            reinterpret_cast<mapped_view_reference<unsigned char const> const &>( range );
        #else // compiler might care about strict aliasing rules
            mapped_view_reference<unsigned char const>
            (
                static_cast<unsigned char const *>( static_cast<void const *>( range.begin() ) ),
                static_cast<unsigned char const *>( static_cast<void const *>( range.end  () ) )
            );
        #endif // compiler
    }


    template <typename Element>
    mapped_view_reference<Element>
    #ifdef BOOST_MSVC
        const &
    #endif
    mapped_view_base<Element>::make_typed_range( mapped_view_base<unsigned char> const & range )
    {
        BOOST_ASSERT( reinterpret_cast<std::size_t>( range.begin() ) % sizeof( Element ) == 0 );
        BOOST_ASSERT( reinterpret_cast<std::size_t>( range.end  () ) % sizeof( Element ) == 0 );
        BOOST_ASSERT(                                range.size ()   % sizeof( Element ) == 0 );
        return
        #ifdef BOOST_MSVC
            reinterpret_cast<mapped_view_reference<Element> const &>( range );
        #else // compiler might care about strict aliasing rules
            mapped_view_reference<unsigned char const>
            (
                static_cast<Element *>( static_cast<void *>( range.begin() ) ),
                static_cast<Element *>( static_cast<void *>( range.end  () ) )
            );
        #endif // compiler
    }
} // namespace detail


template <typename Handle>
struct is_mappable : mpl::false_ {};

template <> struct is_mappable<char                                  *> : mpl::true_ {};
template <> struct is_mappable<char                            const *> : mpl::true_ {};
template <> struct is_mappable<FILE                                  *> : mpl::true_ {};
template <> struct is_mappable<guard::posix_handle::handle_t          > : mpl::true_ {};
#ifdef _WIN32
template <> struct is_mappable<wchar_t                               *> : mpl::true_ {};
template <> struct is_mappable<wchar_t                         const *> : mpl::true_ {};
template <> struct is_mappable<guard::windows_handle::handle_t        > : mpl::true_ {};
#endif // _WIN32


template <>
mapped_view_reference<unsigned char> mapped_view_reference<unsigned char>::map
(
    guard::native_handle_t,
    mapping_flags const &,
    std::size_t desired_size,
    std::size_t offset
);

template <>
mapped_view_reference<unsigned char const> mapped_view_reference<unsigned char const>::map
(
    guard::native_handle_t object_handle,
    std::size_t            desired_size,
    std::size_t            offset,
    bool                   map_for_code_execution
);


template <typename Element>
class mapped_view
    :
    public  mapped_view_reference<Element>
    #ifdef BOOST_MSVC
        ,private noncopyable
    #endif // BOOST_MSVC
{
public:
    mapped_view( mapped_view_reference<Element> const range ) : mapped_view_reference<Element>( range ) {}
    ~mapped_view<Element>() { boost::mapped_view_reference<Element>::unmap( *this ); }

    #ifndef BOOST_MSVC
        mapped_view( mapped_view const & ); // noncopyable
    #endif // BOOST_MSVC
};

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#define BOOST_MMAP_IMPL_FILE "mapped_view.inl"
#include "detail/include_impl_file.hpp"

#endif // mapped_view_hpp
