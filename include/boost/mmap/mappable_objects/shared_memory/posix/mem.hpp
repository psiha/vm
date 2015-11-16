////////////////////////////////////////////////////////////////////////////////
///
/// \file posix/mem.hpp
/// -------------------
///
/// Copyright (c) Domagoj Saric 2015.
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
#ifndef mem_hpp__448C1250_E77A_4945_87A2_087793290E07
#define mem_hpp__448C1250_E77A_4945_87A2_087793290E07
#pragma once
//------------------------------------------------------------------------------
#include "flags.hpp"

#include <cstring>

#include <boost/mmap/detail/impl_selection.hpp>
#include <boost/mmap/mapping/mapping.hpp>
#include <boost/mmap/error/error.hpp>
#include <boost/mmap/handles/handle.hpp>
#include <boost/mmap/mappable_objects/shared_memory/policies.hpp>

#include <sys/mman.h>

#if defined( __ANDROID__ )
#include "../android/mem.hpp"
#elif __has_include( <sys/posix_shm.h> )
// itimerval http://lists.freebsd.org/pipermail/freebsd-arch/2014-July/015531.html
#include <sys/time.h>
#include <sys/posix_shm.h>
#endif // posix_shm.h

#if __has_include( <sys/memfd.h> )
// http://man7.org/linux/man-pages/man2/memfd_create.2.html
#include <sys/memfd.h>
//MFD_CLOEXEC
#endif // memfd

#include <climits>
#include <cstddef>
#include <type_traits>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
BOOST_MMAP_POSIX_INLINE
namespace posix
{
//------------------------------------------------------------------------------

template <class Handle> struct is_resizable;

#ifndef __ANDROID__
namespace detail
{
    // http://lists.apple.com/archives/darwin-development/2003/Mar/msg00242.html
    // http://insanecoding.blogspot.hr/2007/11/pathmax-simply-isnt.html
    std::size_t constexpr max_shm_name =
    #if defined( SHM_NAME_MAX ) // OSX
        SHM_NAME_MAX;
    #elif defined( PSHMNAMLEN ) // OSX, BSD
        PSHMNAMLEN;
    #else
        NAME_MAX;
    #endif // SHM_NAME_MAX

    void BOOST_CC_REG slash_name( char const * const name, char * const slashed_name, std::uint8_t const name_length )
    {
        slashed_name[ 0 ] = '/';
        BOOST_ASSUME( name[ name_length ] == '\0' );
        std::memcpy( slashed_name + 1, name, name_length + 1 );
    }

    // FreeBSD extension SHM_ANON http://www.freebsd.org/cgi/man.cgi?query=shm_open
    file_handle::reference BOOST_CC_REG shm_open_slashed
    (
        char                 const * const slashed_name,
        std::size_t                  const size,
        flags::shared_memory const &       flags
    )
    {
        // shm_open+frunc race condition
        // http://stackoverflow.com/questions/16502767/shm-open-and-ftruncate-race-condition-possible
        // http://stackoverflow.com/questions/20501290/using-fstat-between-shm-open-and-mmap
        // https://developer.apple.com/library/ios/documentation/System/Conceptual/ManPages_iPhoneOS/man2/shm_open.2.html

        // strange OSX behaviour (+ O_TRUNC does not seem to work at all -> EINVAL)
        // http://lists.apple.com/archives/darwin-development/2003/Oct/msg00187.html

        auto const oflags         ( flags.ap.oflag() | static_cast<flags::flags_t>( flags.nocp ) );
        auto const mode           ( flags.ap.pmode()                                             );
        auto       file_descriptor( ::shm_open( slashed_name, oflags, mode )                     );
        if ( BOOST_LIKELY( file_descriptor != -1 ) )
        {
            if ( BOOST_UNLIKELY( ::ftruncate( file_descriptor, size ) != 0 ) )
            {
                BOOST_VERIFY( ::shm_unlink( slashed_name   ) == 0 );
                BOOST_VERIFY( ::close     ( file_descriptor) == 0 );
                file_descriptor = -1;
            }
        }
        return { file_descriptor };
    }

    file_handle::reference BOOST_CC_REG shm_open
    (
        char const * const name,
        std::size_t const size,
        flags::shared_memory const & flags
    )
    {
        auto const length( std::strlen( name ) ); //...todo...constexpr...https://www.daniweb.com/software-development/cpp/code/482276/c-11-compile-time-string-concatenation-with-constexpr
        char slashed_name[ 1 + length + 1 ];
        slash_name( name, slashed_name, length );
        return shm_open_slashed( slashed_name, size, flags );
    }
} // namespace detail


////////////////////////////////////////////////////////////////////////////////
/// native_named_memory (persistent, resizable)
////////////////////////////////////////////////////////////////////////////////

class native_named_memory
    :
    public mapping
{
private:
    using base_t = mapping;
    using mflags = flags::shared_memory;

public:
    using base_t::base_t;

    native_named_memory
    (
        char        const * const name,
        std::size_t         const size,
        mflags              const flags,
        std::nothrow_t
    ) noexcept( true )
        : base_t( detail::shm_open( name, size, flags ), flags, size )
    {}

    native_named_memory
    (
        char        const * const name,
        std::size_t         const size,
        mflags              const flags
    )
        : base_t( detail::shm_open( name, size, flags ), flags, size )
    {
        if ( BOOST_UNLIKELY( !*this ) )
            err::make_and_throw_exception<error>();
    }

    static
    fallible_result<native_named_memory> BOOST_CC_REG create
    (
        char        const * const name,
        std::size_t         const size,
        mflags              const flags
    ) noexcept
    {
        return native_named_memory{ name, size, flags, std::nothrow };
    }

    static bool BOOST_CC_REG cleanup( char const * const name ) noexcept
    {
        auto const length( std::strlen( name ) );
        char slashed_name[ 1 + length + 1 ];
        detail::slash_name( name, slashed_name, length );
        auto const result( ::shm_unlink( slashed_name ) );
        if ( result != error::no_error )
        {
            BOOST_ASSERT( result == ENOENT );
            return false;
        }
        return true;
    }

    fallible_result<std::size_t> size() const noexcept { return get_size( *this ); }

private:
}; // class native_named_memory


namespace detail
{
    // http://charette.no-ip.com:81/programming/2010-01-13_PosixSemaphores
    // http://heldercorreia.com/blog/semaphores-in-mac-os-x
    class named_semaphore
    {
    protected:
        named_semaphore( char const * name, /*::mode_t*/flags::access_privileges::system, flags::named_object_construction_policy ) noexcept;
        named_semaphore( named_semaphore const & ) = delete;
        named_semaphore( named_semaphore && other ) noexcept : semid_( other.semid_ ) { other.semid_ = -1; }

        ~named_semaphore() noexcept;

        //named_semaphore & operator++() { BOOST_VERIFY( semadd( +1 ) ); return *this; }
        //named_semaphore & operator--() { BOOST_VERIFY( semadd( -1 ) ); return *this; }

        void BOOST_CC_REG remove() noexcept;

        bool BOOST_CC_REG semadd( int value, bool nowait = false ) noexcept;
        bool BOOST_CC_REG try_wait() { return semadd( -1, true ); }

        std::uint16_t BOOST_CC_REG value() const noexcept;

    public:
        explicit operator bool() const { return semid_ != -1; }

    private:
        named_semaphore( int const id ) : semid_( id ) {}

        bool BOOST_CC_REG is_initialised() const noexcept;

        bool BOOST_CC_REG semop( int opcode, bool nowait = false ) noexcept;

    private:
        int semid_;
    }; // class named_semaphore

    using named_memory_guard = named_semaphore;
    using shm_name_t         = std::unique_ptr<char[]>;

    ////////////////////////////////////////////////////////////////////////////
    /// scoped_named_memory (scoped, resizable)
    ////////////////////////////////////////////////////////////////////////////

    class scoped_named_memory
        :
        private named_memory_guard,
        private shm_name_t,
        public  native_named_memory
    {
    private:
        using base_t = native_named_memory ;
        using mflags = flags::shared_memory;

    public:
        scoped_named_memory() = default;

        scoped_named_memory
        (
            char        const * const name,
            std::size_t         const size,
            mflags              const flags,
            std::nothrow_t
        ) noexcept
            :
            named_memory_guard( name, flags.ap.system_access, flags.nocp ),
            shm_name_t        ( conditional_make_slashed_name( name ) ),
            base_t            ( conditional_make_shm_fd      ( size, flags ), flags, size )
        {}

        scoped_named_memory
        (
            char        const * const name,
            std::size_t         const size,
            mflags              const flags
        ) noexcept( false )
            : scoped_named_memory( name, size, flags, std::nothrow_t() )
        {
            if ( BOOST_UNLIKELY( !*this ) )
                err::make_and_throw_exception<error>();
        }

        scoped_named_memory( scoped_named_memory && other )
            : named_memory_guard( std::move( other ) ), shm_name_t( std::move( other ) ), native_named_memory( std::move( other ) ) {}

        scoped_named_memory( scoped_named_memory const & ) = delete;

        static
        fallible_result<scoped_named_memory> BOOST_CC_REG create
        (
            char        const * const name,
            std::size_t         const size,
            mflags              const flags
        ) noexcept
        {
            return scoped_named_memory{ name, size, flags, std::nothrow };
        }

        static
        fallible_result<scoped_named_memory> BOOST_CC_REG open( char const * const name, mflags const flags, std::size_t const size ) noexcept; // todo

        ~scoped_named_memory()
        {
            if ( static_cast<handle const &>( *this ) )
            {
                /// \note Global/system-wide (semaphore) reference count.
                ///                               (03.10.2015.) (Domagoj Saric)
                BOOST_VERIFY( named_memory_guard::semadd( -1, true ) );
                if ( named_memory_guard::value() == 0 )
                {
                    BOOST_VERIFY( ::shm_unlink( shm_name_t::get() ) == 0 );
                    named_memory_guard::remove();
                }
            }
        }

        fallible_result<std::size_t> size() const noexcept { return get_size( *this ); }

        using base_t::operator bool;

    private:
        detail::shm_name_t conditional_make_slashed_name( char const * const name ) const __restrict
        {
            if ( BOOST_UNLIKELY( !static_cast<named_memory_guard const &>( *this ) ) )
                return nullptr;
            auto const length( std::strlen( name ) );
            auto const slashed_name( new ( std::nothrow ) char[ 1 + length + 1 ] );
            if ( BOOST_LIKELY( slashed_name != nullptr ) )
                detail::slash_name( name, slashed_name, length );
            else
                error::set( ENOMEM );
            return detail::shm_name_t( slashed_name );
        }

        file_handle::reference conditional_make_shm_fd( std::size_t const length, flags::shared_memory const & flags ) const __restrict
        {
            auto const & name( static_cast<shm_name_t const &>( *this ) );
            if ( BOOST_LIKELY( name != nullptr ) )
                return detail::shm_open( name.get(), length, flags );
            return { file_handle::traits::invalid_value };
        }
    }; // class scoped_named_memory
} // namespace detail


namespace detail
{
    template <typename T> using identity = std::remove_reference<T>;

    template <lifetime_policy lifetime, resizing_policy resizability>
    struct named_memory_impl : identity<posix::native_named_memory> {};

    template <resizing_policy resizability>
    struct named_memory_impl<lifetime_policy::scoped, resizability> : identity<posix::detail::scoped_named_memory> {};
} // namespace detail
#endif // __ANDROID__

template <>
struct is_resizable<file_handle> : std::true_type {};


mapping BOOST_CC_REG create_mapping( handle::reference, mapping ) noexcept;

//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
#   include "mem.inl"
#endif // BOOST_MMAP_HEADER_ONLY

#endif // mem_hpp
