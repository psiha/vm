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
#include "handles/handle.hpp"
#include "detail/impl_selection.hpp"

#include "boost/assert.hpp"
#include "boost/noncopyable.hpp"
#include "boost/range/iterator_range_core.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

typedef iterator_range<unsigned char       *> basic_memory_range_t;
typedef iterator_range<unsigned char const *> basic_read_only_memory_range_t;

template <typename Element, typename Impl = BOOST_MMAP_IMPL()>
class mapped_view_reference;

typedef mapped_view_reference<unsigned char      > basic_mapped_view_ref;
typedef mapped_view_reference<unsigned char const> basic_mapped_read_only_view_ref;

namespace detail
{
    template <typename Element, typename Impl>
    class mapped_view_base : public iterator_range<Element *>
    {
    public:
        typedef iterator_range<Element *> memory_range_t;

    public: // Factory methods.
        static void unmap( mapped_view_base const & );

    protected:
        mapped_view_base( iterator_range<Element *> const & mapped_range ) : iterator_range<Element *>( mapped_range   ) {}
        mapped_view_base( Element * const p_begin, Element * const p_end ) : iterator_range<Element *>( p_begin, p_end ) {}

        static mapped_view_reference<unsigned char const, Impl>
        #ifdef BOOST_MSVC
            const &
        #endif
        make_basic_view( mapped_view_base<Element, Impl> const & );

        static mapped_view_reference<Element, Impl>
        #ifdef BOOST_MSVC
            const &
        #endif
        make_typed_range( mapped_view_base<unsigned char, Impl> const & );

    private: // Hide mutable members
        void advance_begin();
        void advance_end  ();

        void pop_front();
        void pop_back ();
    };


    template <typename Element, typename Impl>
    void mapped_view_base<Element, Impl>::unmap( mapped_view_base<Element, Impl> const & mapped_range )
    {
        mapped_view_base<unsigned char const, Impl>::unmap( make_basic_view( mapped_range ) );
    }
} // namespace detail

template <typename Impl> struct mapping;

template <typename Element, typename Impl>
class mapped_view_reference : public detail::mapped_view_base<Element, Impl>
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
        mapping<Impl>   const & source_mapping,
        boost::uint64_t         offset       = 0,
        std  ::size_t           desired_size = 0
    );

private:
    template <typename Element_, typename Impl_> friend class detail::mapped_view_base;

    mapped_view_reference( iterator_range<Element *> const & mapped_range ) : detail::mapped_view_base<Element, Impl>( mapped_range   ) {}
    mapped_view_reference( Element * const p_begin, Element * const p_end ) : detail::mapped_view_base<Element, Impl>( p_begin, p_end ) {}
};


//...zzz...
//template <typename Element, typename Impl>
//basic_read_only_memory_range_t mapped_view_reference<Element const, Impl>::basic_range() const
//{
//    return basic_read_only_memory_range_t
//    (
//        static_cast<unsigned char const *>( static_cast<void const *>( this->begin() ) ),
//        static_cast<unsigned char const *>( static_cast<void const *>( this->end  () ) )
//    );
//}


namespace detail
{
    // Implementation note:
    //   These have to be defined after mapped_view_reference for eager
    // compilers (e.g. GCC and Clang).
    //                                        (14.07.2011.) (Domagoj Saric)

    template <typename Element, typename Impl>
    mapped_view_reference<unsigned char const, Impl>
    #ifdef BOOST_MSVC
        const &
    #endif
    mapped_view_base<Element, Impl>::make_basic_view( mapped_view_base<Element, Impl> const & range )
    {
        return
        #ifdef BOOST_MSVC
            reinterpret_cast<mapped_view_reference<unsigned char const, Impl> const &>( range );
        #else // compiler might care about strict aliasing rules
            mapped_view_reference<unsigned char const, Impl>
            (
                static_cast<unsigned char const *>( static_cast<void const *>( range.begin() ) ),
                static_cast<unsigned char const *>( static_cast<void const *>( range.end  () ) )
            );
        #endif // compiler
    }


    template <typename Element, typename Impl>
    mapped_view_reference<Element, Impl>
    #ifdef BOOST_MSVC
        const &
    #endif
    mapped_view_base<Element, Impl>::make_typed_range( mapped_view_base<unsigned char, Impl> const & range )
    {
        BOOST_ASSERT( reinterpret_cast<std::size_t>( range.begin() ) % sizeof( Element ) == 0 );
        BOOST_ASSERT( reinterpret_cast<std::size_t>( range.end  () ) % sizeof( Element ) == 0 );
        BOOST_ASSERT(                                range.size ()   % sizeof( Element ) == 0 );
        return
        #ifdef BOOST_MSVC
            reinterpret_cast<mapped_view_reference<Element, Impl> const &>( range );
        #else // compiler might care about strict aliasing rules
            mapped_view_reference<Element, Impl>
            (
                static_cast<Element *>( static_cast<void *>( range.begin() ) ),
                static_cast<Element *>( static_cast<void *>( range.end  () ) )
            );
        #endif // compiler
    }
} // namespace detail


template <typename Handle>
struct is_mappable : mpl::false_ {};

template <> struct is_mappable<char                                 *> : mpl::true_ {};
template <> struct is_mappable<char                           const *> : mpl::true_ {};
template <> struct is_mappable<FILE                                 *> : mpl::true_ {};
template <> struct is_mappable<handle<posix>::native_handle_t        > : mpl::true_ {};
#ifdef _WIN32
template <> struct is_mappable<wchar_t                              *> : mpl::true_ {};
template <> struct is_mappable<wchar_t                        const *> : mpl::true_ {};
template <> struct is_mappable<handle<win32>::native_handle_t        > : mpl::true_ {};
#endif // _WIN32

template <typename Element, typename Impl>
mapped_view_reference<Element, Impl> mapped_view_reference<Element, Impl>::map
(
    mapping<Impl>   const & source_mapping,
    boost::uint64_t         offset        ,
    std  ::size_t           desired_size
)
{
    BOOST_ASSERT_MSG( !boost::is_const<Element>::value || source_mapping.is_read_only(), "Use const element mapped view for read only mappings." );
    return mapped_view_reference<unsigned char, Impl>::map( source_mapping, offset, desired_size );
}

//...zzz...
//template <unsigned char, typename Impl>
//static mapped_view_reference<unsigned char, Impl> mapped_view_reference<unsigned char, Impl>::map
//(
//    mapping<Impl>   const & source_mapping,
//    boost::uint64_t         offset       = 0,
//    std  ::size_t           desired_size = 0
//);


template <typename Element, typename Impl = BOOST_MMAP_IMPL()>
class mapped_view
    :
    public  mapped_view_reference<Element, Impl>
    #ifdef BOOST_MSVC
        ,private noncopyable
    #endif // BOOST_MSVC
{
public:
    mapped_view( mapped_view_reference<Element, Impl> const range ) : mapped_view_reference<Element, Impl>( range ) {}
    ~mapped_view() { mapped_view_reference<Element, Impl>::unmap( *this ); }

    #ifndef BOOST_MSVC
        mapped_view( mapped_view const & ); // noncopyable
    #endif // BOOST_MSVC
};


basic_mapped_view_ref           map_file          ( char const * file_name, std::size_t desired_size );
basic_mapped_read_only_view_ref map_read_only_file( char const * file_name                           );

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "mapped_view.inl"
#endif

#endif // mapped_view_hpp
