////////////////////////////////////////////////////////////////////////////////
///
/// \file posix/flags.hpp
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
#ifndef flags_hpp__3E991311_2199_4C61_A484_B8b72C528B0F
#define flags_hpp__3E991311_2199_4C61_A484_B8b72C528B0F
#pragma once
//------------------------------------------------------------------------------
#include "psi/vm/detail/impl_selection.hpp"
#include "psi/vm/detail/posix.hpp"
#include "psi/vm/flags/flags.hpp"

#include "fcntl.h"
#include "sys/mman.h" // PROT_* constants
#include "sys/stat.h" // umask

#include <array>
#include <cstdint>
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
namespace flags
{
//------------------------------------------------------------------------------

using flags_t = int;

enum struct named_object_construction_policy : flags_t // creation_disposition
{
    create_new                      = O_CREAT | O_EXCL ,
    create_new_or_truncate_existing = O_CREAT | O_TRUNC,
    open_existing                   = 0                ,
    open_or_create                  = O_CREAT          ,
    open_and_truncate_existing      = O_TRUNC
}; // struct named_object_construction_policy

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

struct access_privileges
{
private:
    static std::uint8_t constexpr syssh  =  0;
    static std::uint8_t constexpr procsh = 16;
    static std::uint8_t constexpr mapsh  = 24;

#ifndef O_EXEC
    static std::uint8_t constexpr O_EXEC = O_RDONLY;
#elif defined( __EMSCRIPTEN__ )
    // emscripten defines O_EXEC as O_PATH, which is 010000000, which causes compile error
    //  error: enumerator value is not a constant expression
    // due to a
    // note: signed left shift discards bits
    #undef O_EXEC
    static std::uint8_t constexpr O_EXEC = O_RDONLY;
#endif // O_EXEC

#if O_RDONLY
    static std::uint8_t constexpr O_RDONLY_ = O_RDONLY;
#else // "Undetectable combined O_RDONLY" http://linux.die.net/man/3/open
    static std::uint8_t constexpr O_RDONLY_ = static_cast< std::uint8_t >( O_RDONLY + O_WRONLY + O_RDWR + O_EXEC + 1 );
    static_assert( ( O_RDONLY_ & ( O_RDONLY | O_WRONLY | O_RDWR | O_EXEC ) ) == 0, "" );
#endif // O_RDONLY

    using sys_flags = detail::rwx_flags;

public:
    enum value_type : std::uint32_t
    {
        /// \note Combine file and mapping flags/bits in order to be able to use
        /// the same flags for all objects (i.e. like on POSIX systems).
        ///                                   (08.09.2015.) (Domagoj Saric)
        //          SYSTEM                        | PROCESS             | MAPPING
        metaread  = 0                    << syssh | static_cast< std::uint32_t >( 0         ) << procsh | static_cast< std::uint32_t >(  PROT_NONE  ) << mapsh,
        read      = sys_flags::read      << syssh | static_cast< std::uint32_t >( O_RDONLY_ ) << procsh | static_cast< std::uint32_t >(  PROT_READ  ) << mapsh,
        write     = sys_flags::write     << syssh | static_cast< std::uint32_t >( O_WRONLY  ) << procsh | static_cast< std::uint32_t >(  PROT_WRITE ) << mapsh,
        execute   = sys_flags::execute   << syssh | static_cast< std::uint32_t >( O_EXEC    ) << procsh | static_cast< std::uint32_t >(  PROT_EXEC  ) << mapsh,
        readwrite = sys_flags::readwrite << syssh | static_cast< std::uint32_t >( O_RDWR    ) << procsh | static_cast< std::uint32_t >(  ( PROT_READ | PROT_WRITE             ) ) << mapsh,
        all       = sys_flags::all       << syssh | static_cast< std::uint32_t >( O_RDWR    ) << procsh | static_cast< std::uint32_t >(  ( PROT_READ | PROT_WRITE | PROT_EXEC ) ) << mapsh
    };

    constexpr static bool unrestricted( flags_t const privileges ) { return ( ( static_cast< std::uint32_t >( privileges ) & all ) == all ); }

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
            // Broken/not thread safe
            // http://man7.org/linux/man-pages/man2/umask.2.html @ notes
            // https://groups.google.com/forum/#!topic/comp.unix.programmer/v6nv-oP9IJQ
            // https://stackoverflow.com/questions/53227072/reading-umask-thread-safe/53288382
            auto const mask( ::umask( 0 ) );
            BOOST_VERIFY( ::umask( mask ) == 0 );
            return static_cast< flags_t >( mask );
        }

        operator flags_t() const noexcept { return flags; }

        static system const process_default;
        static system const unrestricted   ;
        static system const nix_default    ;
        static system const _644           ;

        flags_t flags;
    }; // struct system

    flags_t BOOST_CC_REG oflag() const noexcept;
    mode_t  BOOST_CC_REG pmode() const noexcept { return static_cast< mode_t >( system_access.flags ); }

    object        object_access;
    child_process child_access ;
    system        system_access;
}; // struct access_privileges


constexpr access_privileges::system const access_privileges::system::process_default{ static_cast<flags_t>( sys_flags::all ) };
constexpr access_privileges::system const access_privileges::system::unrestricted   { static_cast<flags_t>( privilege_scopes::user ) | static_cast<flags_t>( privilege_scopes::group ) | static_cast<flags_t>( privilege_scopes::world ) };
constexpr access_privileges::system const access_privileges::system::nix_default    { S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH };
constexpr access_privileges::system const access_privileges::system::_644           ( nix_default );

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace vm
//------------------------------------------------------------------------------
} // namespace psi
//------------------------------------------------------------------------------

#ifdef PSI_VM_HEADER_ONLY
    #include "flags.inl"
#endif // PSI_VM_HEADER_ONLY

#endif // opening_hpp
