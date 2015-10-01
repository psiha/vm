////////////////////////////////////////////////////////////////////////////////
///
/// \file posix/flags.hpp
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
#ifndef flags_hpp__3E991311_2199_4C61_A484_B8b72C528B0F
#define flags_hpp__3E991311_2199_4C61_A484_B8b72C528B0F
#pragma once
//------------------------------------------------------------------------------
#include "boost/mmap/detail/posix.hpp"
#include "boost/mmap/implementations.hpp"
#include "boost/mmap/flags/flags.hpp"

#include "sys/fcntl.h"
#include "sys/mman.h" // PROT_* constants
#include "sys/stat.h" // umask

#include <array>
#include <cstdint>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------ 
namespace flags
{
//------------------------------------------------------------------------------

template <typename Impl> struct opening;

using flags_t = int;

template <>
struct named_object_construction_policy<posix> // creation_disposition
{
    enum /*struct*/ flags : flags_t
    {
        create_new                      = O_CREAT | O_EXCL ,
        create_new_or_truncate_existing = O_CREAT | O_TRUNC,
        open_existing                   = 0                ,
        open_or_create                  = O_CREAT          ,
        open_and_truncate_existing      = O_TRUNC
    };
    using value_type = flags;

    value_type value;
}; // struct named_object_construction_policy<posix>

namespace detail
{
    enum /*struct*/ rwx_flags : mode_t
    {
    #ifdef _MSC_VER
        read    = _S_IREAD ,
        write   = _S_IWRITE,
        execute = _S_IEXEC ,
    #else
        read    = S_IRUSR | S_IRGRP | S_IROTH,
        write   = S_IWUSR | S_IWGRP | S_IWOTH,
        execute = S_IXUSR | S_IXGRP | S_IXOTH,
    #endif // _MSC_VER
        readwrite = read | write,
        all       = read | write | execute
    }; // enum /*struct*/ rwx_flags

    enum struct privilege_scopes { user, group, world, count, combined };

#if 0 //...mrmlj...todo...
    void make_child_process_inheritable()
    {
        // http://blogs.msdn.com/b/oldnewthing/archive/2011/12/16/10248328.aspx
        //BOOST_VERIFY( ::SetHandleInformation( handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT ) );

        // http://www.gnu.org/software/libc/manual/html_node/Descriptor-Flags.html
        // http://stackoverflow.com/questions/18306072/open-doesnt-set-o-cloexec-flag
        //BOOST_VERIFY( ::fcntl( fd, F_SETFD, FD_CLOEXEC ) != -1 );
        //O_CLOEXEC
    }
#endif
} // namespace detail

template <>
struct access_privileges<posix>
{
private:
    static std::uint8_t constexpr syssh  =  0;
    static std::uint8_t constexpr procsh = 16;
    static std::uint8_t constexpr mapsh  = 24;

#ifndef O_EXEC
    static std::uint8_t constexpr O_EXEC = O_RDONLY;
#endif // O_EXEC

#if O_RDONLY
    static std::uint8_t constexpr O_RDONLY_ = O_RDONLY;
#else // "Undetectable combined O_RDONLY" http://linux.die.net/man/3/open
    static std::uint8_t constexpr O_RDONLY_ = O_RDONLY + O_WRONLY + O_RDWR + O_EXEC + 1;
    static_assert( ( O_RDONLY_ & ( O_RDONLY | O_WRONLY | O_RDWR | O_EXEC ) ) == 0, "" );
#endif // O_RDONLY

    using sys_flags = detail::rwx_flags;

public:
    enum flags : std::uint32_t
    {
        /// \note Combine file and mapping flags/bits in order to be able to use
        /// the same flags for all objects (i.e. like on POSIX systems).
        ///                                   (08.09.2015.) (Domagoj Saric)
        //          SYSTEM                        | PROCESS             | MAPPING
        metaread  = 0                    << syssh | 0         << procsh | PROT_NONE  << mapsh,
        read      = sys_flags::read      << syssh | O_RDONLY_ << procsh | PROT_READ  << mapsh,
        write     = sys_flags::write     << syssh | O_WRONLY  << procsh | PROT_WRITE << mapsh,
        execute   = sys_flags::execute   << syssh | O_EXEC    << procsh | PROT_EXEC  << mapsh,
        readwrite = sys_flags::readwrite << syssh | O_RDWR    << procsh | ( PROT_READ | PROT_WRITE             ) << mapsh,
        all       = sys_flags::all       << syssh | O_RDWR    << procsh | ( PROT_READ | PROT_WRITE | PROT_EXEC ) << mapsh
    };
    using value_type = flags;

    constexpr static bool unrestricted( flags_t const privileges ) { return ( ( privileges & all ) == all ); }

    struct object
    {
        flags_t /*const*/ privileges;

        flags_t BOOST_CC_REG protection() const noexcept { return ( privileges >> mapsh ) & 0xFF; }
    }; // struct process

    enum struct child_process : flags_t
    {
        does_not_inherit = O_CLOEXEC,
        inherits         = 0
    }; // enum struct child_process


    struct system
    {
    private:
        enum struct privilege_scopes : flags_t
        {
            user     = S_IRUSR | S_IWUSR | S_IXUSR,
            group    = S_IRGRP | S_IWGRP | S_IXGRP,
            world    = S_IROTH | S_IWOTH | S_IXOTH,
            combined = static_cast<flags_t>( -1 )
        };

        template <privilege_scopes scope_mask>
        struct scoped_privileges
        {
            constexpr scoped_privileges( flags_t const flags )
                : flags( flags & static_cast<flags_t>( scope_mask ) ) {}

            template <privilege_scopes scope>
            constexpr
            scoped_privileges<privilege_scopes::combined> operator | ( scoped_privileges<scope> const scope_flags ) const
            {
                return { flags | scope_flags.flags };
            }

            constexpr bool unrestricted() const { return unrestricted( flags ); }

            flags_t const flags;
        }; // struct scoped_privileges

    public:
        using user  = scoped_privileges<privilege_scopes::user >;
        using group = scoped_privileges<privilege_scopes::group>;
        using world = scoped_privileges<privilege_scopes::world>;

        flags_t read_umask()
        {
            auto const mask( ::umask( 0 ) );
            BOOST_VERIFY( ::umask( mask ) == 0 );
            return mask;
        }

        static system const process_default;
        static system const unrestricted   ;
        static system const nix_default    ;
        static system const _644           ;

        flags_t flags;
    }; // struct system

    flags_t BOOST_CC_REG oflag() const noexcept;
    mode_t  BOOST_CC_REG pmode() const noexcept { return system_access.flags; }

    object        object_access;
    child_process child_access ;
    system        system_access;
}; // struct access_privileges<posix>


constexpr access_privileges<posix>::system const access_privileges<posix>::system::process_default = { static_cast<flags_t>( sys_flags::all ) };
constexpr access_privileges<posix>::system const access_privileges<posix>::system::unrestricted    = { static_cast<flags_t>( privilege_scopes::user ) | static_cast<flags_t>( privilege_scopes::group ) | static_cast<flags_t>( privilege_scopes::world ) };
constexpr access_privileges<posix>::system const access_privileges<posix>::system::nix_default     = { S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH };
constexpr access_privileges<posix>::system const access_privileges<posix>::system::_644            = { nix_default };

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "flags.inl"
#endif // BOOST_MMAP_HEADER_ONLY

#endif // opening_hpp
