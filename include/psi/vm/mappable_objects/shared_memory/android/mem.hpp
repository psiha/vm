////////////////////////////////////////////////////////////////////////////////
///
/// \file android/mem.hpp
/// ---------------------
///
/// Copyright (c) Domagoj Saric 2015 - 2024.
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
#ifndef mem_hpp__497A6A4E_4630_4841_BAEE_2A26498ABF6A
#define mem_hpp__497A6A4E_4630_4841_BAEE_2A26498ABF6A
#pragma once
//------------------------------------------------------------------------------
#include "flags.hpp"

#include <psi/vm/detail/impl_selection.hpp>
#include <psi/vm/mapping/mapping.hpp>
#include <psi/vm/error/error.hpp>
#include <psi/vm/handles/handle.hpp>
#include <psi/vm/mappable_objects/shared_memory/policies.hpp>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/ashmem.h>

#include <climits>
#include <cstddef>
#include <string_view>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi
{
//------------------------------------------------------------------------------
namespace vm
{
//------------------------------------------------------------------------------
PSI_VM_POSIX_INLINE
namespace posix
{
//------------------------------------------------------------------------------

namespace detail
{
    /// \note Android has no support for POSIX or SysV shared memory but a
    /// custom solution - 'native_named_memory'.
    /// http://notjustburritos.tumblr.com/post/21442138796/an-introduction-to-android-shared-memory
    /// http://elinux.org/Android_Kernel_Features
    /// http://stackoverflow.com/questions/17510157/shm-replacement-based-on-native_named_memory
    /// https://github.com/pelya/android-shmem
    /// http://stackoverflow.com/questions/12864778/shared-memory-region-in-ndk
    /// http://stackoverflow.com/questions/17744108/cutils-not-included-in-ndk?rq=1
    ///                                       (03.10.2015.) (Domagoj Saric)
    static std::string_view constexpr shm_prefix{ ASHMEM_NAME_DEF "/", sizeof( ASHMEM_NAME_DEF "/" ) - 1 };
    static std::string_view           shm_emulated_path{ "/mnt/sdcard/shm" };
    void BOOST_CC_REG prefix_shm_name( char const * const name, char * const prefixed_name, std::uint8_t const name_length, std::string_view const prefix ) noexcept
    {
        std::copy  ( prefix.begin(), prefix.end(), prefixed_name );
        std::memcpy( prefixed_name + prefix.size(), name, name_length + 1 );
    }
    void BOOST_CC_REG prefix_shm_name( char const * const name, char * const prefixed_name, std::uint8_t const name_length ) noexcept
    {
        std::copy  ( shm_prefix.begin(), shm_prefix.end(), prefixed_name );
        std::memcpy( prefixed_name + shm_prefix.size(), name, name_length + 1 );
    }

    BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_RESTRICTED_FUNCTION_L1, BOOST_EXCEPTIONLESS, BOOST_WARN_UNUSED_RESULT )
    file_handle::reference BOOST_CC_REG shm_open
    (
        char                 const * const name,
        std::size_t                  const size,
        flags::shared_memory const &       flags
    ) noexcept
    {
        auto const oflags( flags.ap.oflag() | static_cast<flags::flags_t>( flags.nocp ) );
        auto const mode  ( flags.ap.pmode()                                             );

        auto file_descriptor( ::open( ASHMEM_NAME_DEF, oflags, mode ) );
        if ( BOOST_LIKELY( file_descriptor != -1 ) )
        {
            if
            (
                BOOST_UNLIKELY( ::ioctl( file_descriptor, ASHMEM_SET_SIZE, size ) != 0 ) ||
                BOOST_UNLIKELY( ::ioctl( file_descriptor, ASHMEM_SET_NAME, name ) != 0 )
                //ioctl( fd_, ASHMEM_SET_PROT_MASK, prot )
            )
            {
                BOOST_VERIFY( ::close( file_descriptor ) == 0 );
                file_descriptor = -1;
            }
        }
        return { file_descriptor };
    }
} // namespace detail


////////////////////////////////////////////////////////////////////////////////
/// Ashmem (scoped, resizable)
////////////////////////////////////////////////////////////////////////////////

class native_named_memory
    :
    public mapping
{
protected:
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
    ) noexcept( false )
        : native_named_memory( name, size, flags, std::nothrow_t() )
    {
        if ( BOOST_UNLIKELY( !*this ) )
            err::make_and_throw_exception<error>();
    }

    native_named_memory( native_named_memory && other ) noexcept : base_t( std::move( other ) ) {}

    static
    fallible_result<native_named_memory> BOOST_CC_REG create
    (
        char        const * const name,
        std::size_t         const size,
        mflags              const flags
    ) noexcept
    {
        return native_named_memory( name, size, flags, std::nothrow_t() );
    }

    std::uint32_t size() const noexcept
    {
        auto const result( ::ioctl( mapping::get(), ASHMEM_GET_SIZE, nullptr ) );
        BOOST_ASSERT( result >= 0 );
        return result;
    }

    fallible_result<void> BOOST_CC_REG resize( std::uint32_t const new_size )
    {
        auto const result( ::ioctl( mapping::get(), ASHMEM_SET_SIZE, new_size ) );
        if ( BOOST_UNLIKELY( result < 0 ) ) return error();
        return err::success;
    }
}; // class native_named_memory


////////////////////////////////////////////////////////////////////////////////
/// Persistent
////////////////////////////////////////////////////////////////////////////////

class file_backed_named_memory
    :
    public mapping
{
private:
    using base_t = mapping;
    using mflags = flags ::shared_memory;

public:
    static
    fallible_result<file_backed_named_memory> BOOST_CC_REG create
    (
        char        const * const name,
        std::size_t         const size,
        mflags              const flags
    ) noexcept
    {
        auto const length( std::strlen( name ) );
        char adjusted_name[ detail::shm_emulated_path.size() + length + 1 ];
        detail::prefix_shm_name( name, adjusted_name, length, detail::shm_emulated_path );
        using hints = flags::access_pattern_optimisation_hints;
        auto file
        (
            create_file
            (
                adjusted_name,
                flags::opening::create
                (
                    flags.ap,
                    flags.nocp,
                    hints::random_access | hints::avoid_caching /*| extra_hints*/
                )
            )
        );
        if ( BOOST_UNLIKELY( !file ) )
            return error();
        if ( BOOST_UNLIKELY( !set_size( file, size ) ) )
        {
            BOOST_VERIFY( delete_file( adjusted_name ) );
            return error();
        }
        return file_backed_named_memory //no variadic constructors in fallible_result yet so we have to explicitly construct here
            { detail::shm_open( name, size, flags ), flags, size, std::move( file ) };
    }

    static bool BOOST_CC_REG cleanup( char const * const name ) noexcept
    {
        auto const length( std::strlen( name ) );
        char adjusted_name[ detail::shm_emulated_path.size() + length + 1 ];
        detail::prefix_shm_name( name, adjusted_name, length, detail::shm_emulated_path );
        return delete_file( adjusted_name );
    }

    static
    fallible_result<file_backed_named_memory> BOOST_CC_REG open( char const * const name, mflags const flags, std::size_t const size ) noexcept;

    auto BOOST_CC_REG   size(                            ) const noexcept { return get_size( file_           ); }
    auto BOOST_CC_REG resize( std::size_t const new_size )       noexcept { return set_size( file_, new_size ); }

private:
    file_backed_named_memory( file_handle::reference const shm, flags::viewing const view_mapping_flags, std::size_t const maximum_size, file_handle && backing_file ) noexcept
        : mapping( shm, view_mapping_flags, maximum_size ), file_( std::move( backing_file ) ) {}

private:
    file_handle file_;
}; // class file_backed_named_memory

namespace detail
{
    template <typename T> using identity = std::remove_reference<T>;

    template <lifetime_policy lifetime, resizing_policy resizability>
    struct named_memory_impl : identity<posix::file_backed_named_memory> {};

    template <resizing_policy resizability>
    struct named_memory_impl<lifetime_policy::scoped, resizability> : identity<posix::native_named_memory> {};
} // namespace detail

//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------

#ifdef PSI_VM_HEADER_ONLY
//#   include "mem.inl"
#endif // PSI_VM_HEADER_ONLY

#endif // mem_hpp
