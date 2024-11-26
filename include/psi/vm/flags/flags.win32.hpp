////////////////////////////////////////////////////////////////////////////////
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
#pragma once

#include <psi/vm/detail/win32.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>
#include <boost/utility/base_from_member.hpp>

#include <array>
#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace win32
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

using flags_t = unsigned long; // DWORD

namespace detail
{
    constexpr inline BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_L3 )
    SECURITY_ATTRIBUTES make_sa( SECURITY_DESCRIPTOR const * __restrict const p_sd, bool const inheritable ) noexcept
    {
        return { sizeof( SECURITY_ATTRIBUTES ), const_cast<SECURITY_DESCRIPTOR *>( p_sd ), inheritable };
    }

    inline
    SECURITY_ATTRIBUTES const * make_sa_ptr( SECURITY_ATTRIBUTES & __restrict sa, SECURITY_DESCRIPTOR const * __restrict const p_sd, bool const inheritable ) noexcept
    {
        if ( BOOST_LIKELY( !inheritable ) && !p_sd )
            return nullptr;
        sa = make_sa( p_sd, inheritable );
        return &sa;
    }
} // namespace detail

enum struct named_object_construction_policy : std::uint8_t // creation_disposition
{
    create_new                      = 1,
    create_new_or_truncate_existing = 2,
    open_existing                   = 3,
    open_or_create                  = 4,
    open_and_truncate_existing      = 5
}; // enum struct named_object_construction_policy

namespace detail
{
    enum struct privilege_scopes { user, group, world, count, combined };

    using scope_privileges = std::array<flags_t, static_cast<std::size_t>( privilege_scopes::count )>;

    struct dynamic_sd
        :
        boost::base_from_member<std::uint8_t>,
        ::SECURITY_DESCRIPTOR
    {
        constexpr
        dynamic_sd() noexcept : boost::base_from_member<std::uint8_t>( std::uint8_t( 0 ) ) {}
        dynamic_sd( dynamic_sd const & ) = delete;
        void reset() noexcept { member = 0; }
        std::uint8_t add_ref() const noexcept { return ++const_cast<std::uint8_t &>( member ); }
        std::uint8_t release() const noexcept { return --const_cast<std::uint8_t &>( member ); }
    }; // struct dynamic_sd

    BOOST_ATTRIBUTES( BOOST_EXCEPTIONLESS, BOOST_RESTRICTED_FUNCTION_RETURN )
    dynamic_sd const * BOOST_CC_REG make_sd( scope_privileges );
} // namespace detail

struct access_privileges
{
    enum value_type : std::uint32_t
    {
        /// \note Combine file and mapping flags/bits in order to be able to use
        /// the same flags for all objects (i.e. like on POSIX systems).
        ///                                   (08.09.2015.) (Domagoj Saric)
        metaread  = 0           | SECTION_QUERY,
        read      = 0x80000000L | 0x0004, // GENERIC_READ    | SECTION_MAP_READ,
        write     = 0x40000000L | 0x0002, // GENERIC_WRITE   | SECTION_MAP_WRITE,
        readwrite = read | write,
        execute   = 0x20000000L | 0x0020, // GENERIC_EXECUTE | SECTION_MAP_EXECUTE_EXPLICIT,
        all       = 0x10000000L | SECTION_ALL_ACCESS | SECTION_MAP_EXECUTE_EXPLICIT // GENERIC_ALL
    };

    constexpr static bool unrestricted( flags_t const privileges ) { return ( ( privileges & all ) == all ) || ( ( privileges & ( readwrite | execute ) ) == ( readwrite | execute ) ); }

    struct object
    {
        flags_t /*const*/ privileges;
    }; // struct object

    // https://msdn.microsoft.com/en-us/library/windows/desktop/ms683463(v=vs.85).aspx handle inheritance
    enum struct child_process : bool
    {
        does_not_inherit = false,
        inherits         = true
    }; // enum struct child_process


    struct system
    {
    private:
        using privileges = access_privileges;

        using privilege_scopes = detail::privilege_scopes;
        using scope_privileges = detail::scope_privileges;

        template <privilege_scopes scope>
        struct scoped_privileges
        {
            constexpr scoped_privileges( flags_t          const flag        ) : flags{ ( scope == privilege_scopes::user ) * flag, ( scope == privilege_scopes::group ) * flag, ( scope == privilege_scopes::world ) * flag } {}
            constexpr scoped_privileges( scope_privileges const flags_param ) : flags( flags_param ) {}

            template <privilege_scopes local_scope>
            constexpr
            scoped_privileges<privilege_scopes::combined> operator | ( scoped_privileges<local_scope> const scope_flags ) const
            {
                return
                {
                    scope_privileges
                    {
                        flags[ 0 ] | scope_flags.flags[ 0 ],
                        flags[ 1 ] | scope_flags.flags[ 1 ],
                        flags[ 2 ] | scope_flags.flags[ 2 ],
                    }
                };
            }

            operator system () const noexcept { return system{ detail::make_sd( flags ), true }; }

            constexpr bool unrestricted() const { return unrestricted( flags[ 0 ] ) & unrestricted( flags[ 1 ] ) & unrestricted( flags[ 2 ] ); }

            constexpr operator scope_privileges const & ( ) const { return flags; }

            scope_privileges const flags;
        }; // struct scoped_privileges

    public:
        using user  = scoped_privileges<privilege_scopes::user >;
        using group = scoped_privileges<privilege_scopes::group>;
        using world = scoped_privileges<privilege_scopes::world>;

      //static system constexpr process_default = { nullptr, false }; //...mrmlj...system (type) not yet defined...
      //static system constexpr process_default = system();
        static system const process_default;
        static system const unrestricted   ;
        static system const nix_default    ;
        static system const _644           ;

        constexpr         system(                                                                             ) noexcept : p_sd( nullptr    ), dynamic( false         ) {}
        constexpr         system( SECURITY_DESCRIPTOR const * __restrict p_sd_param, bool const dynamic_param ) noexcept : p_sd( p_sd_param ), dynamic( dynamic_param ) {}
        BOOST_FORCEINLINE system( system const & __restrict other                                             ) noexcept : p_sd( other.p_sd ), dynamic( other.dynamic )
        {
            if ( dynamic )
            {
                BOOST_ASSUME( p_sd );
                BOOST_ASSUME( other.dynamic );
                BOOST_ASSUME( p_sd != process_default.p_sd );
                BOOST_ASSUME( p_sd != unrestricted   .p_sd );
                get_dynamic_sd().add_ref();
            }
        }

        //...mrmlj...to enable const members...system( system && other ) noexcept : p_sd( other.p_sd ), dynamic( other.dynamic ) { other.p_sd = nullptr; other.dynamic = false; }
        ~system() noexcept
        {
            if ( dynamic )
            {
                BOOST_ASSUME( p_sd );
                BOOST_ASSUME( p_sd != process_default.p_sd );
                BOOST_ASSUME( p_sd != unrestricted   .p_sd );
                auto & sd( get_dynamic_sd() );
        #ifdef __clang__
            #pragma clang diagnostic push
            #pragma clang diagnostic ignored "-Wdelete-incomplete"
        #endif
                if ( BOOST_UNLIKELY( sd.release() ) == 0 )
                    delete static_cast<void const *>( &sd ); // delete through void to silence new-delete-(size-)mismatch sanitizers
        #ifdef __clang__
            #pragma clang diagnostic pop
        #endif
            }
        }

        SECURITY_DESCRIPTOR const * __restrict const p_sd   ;
        bool                                   const dynamic;

    private:
        using dynamic_sd = detail::dynamic_sd;
        dynamic_sd const & get_dynamic_sd() const noexcept
        {
            BOOST_ASSUME( dynamic );
            BOOST_ASSUME( p_sd    );
            return static_cast<dynamic_sd const &>( *p_sd );
        }
    }; // struct system

    object        /*const*/ object_access; // desired_access | flProtect
    child_process /*const*/ child_access ;
    system        /*const*/ system_access; // p_security_attributes
}; // struct access_privileges

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
