////////////////////////////////////////////////////////////////////////////////
///
/// \file flags.hpp
/// ---------------
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
#ifndef flags_hpp__BFFC0541_21AC_4A80_A9EE_E0450B6D4D8A
#define flags_hpp__BFFC0541_21AC_4A80_A9EE_E0450B6D4D8A
#pragma once
//------------------------------------------------------------------------------
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

// Implementation note:
//   Using structs with public members and factory functions to enable (almost)
// zero-overhead (w/ IPO & LTO) conversion to native flag formats and to allow
// the user to modify the created flags or create fully custom ones so that
// specific platform-dependent use-cases, not otherwise covered through the
// generic interface, can also be supported.
// https://msdn.microsoft.com/en-us/library/7572ztz4.aspx
//                                            (10.10.2010.) (Domagoj Saric)


#ifdef DOXYGEN_ONLY
using flags_t = impl-specific;
#endif // DOXYGEN_ONLY


////////////////////////////////////////////////////////////////////////////////
///
/// \struct named_object_construction_policy
/// \brief AKA creation disposition
///
/// Behaviour policies WRT (non)existing same-named objects.
///
////////////////////////////////////////////////////////////////////////////////

template <typename Impl>
struct named_object_construction_policy
#ifdef DOXYGEN_ONLY
{
    enum struct flags : std::uint8_t
    {
        create_new                     , ///< Create a new object or fail if one already exists
        create_new_or_truncate_existing, ///< Create a new object or truncate a possibly existing one (i.e. always-start-with-an-empty-object semantics)
        open_existing                  , ///< Open an object iff it already exists or fail otherwise
        open_or_create                 , ///< Open an object if it already exists or create it otherwise
        open_and_truncate_existing       ///< Open an object, truncating it, iff it already exists or fail otherwise
    };
    using value_type = flags;

    value_type value;
}
#endif // DOXYGEN_ONLY
; // struct named_object_construction_policy


////////////////////////////////////////////////////////////////////////////////
///
/// \struct access_privileges
///
/// \brief
///
////////////////////////////////////////////////////////////////////////////////

template <typename Impl>
struct access_privileges
#ifdef DOXYGEN_ONLY
{
    enum flags : std::uint32_t
    {
        metaread                    ,
        read                        , ///< Allow reading from the mapped region
        write                       , ///< Allow writing to the mapped region
        readwrite                   ,
        execute                     , ///< Allow executing code from the mapped region
        all = read | write | execute  ///< Allow every possible access type
    };
    using value_type = flags;

    /// Object access rights
    struct object
    {
        flags_t privileges;
    }; // struct object

    enum struct child_process
    {
        does_not_inherit,
        inherits
    }; // enum struct child_process

    struct system
    {
        struct user ;
        struct group;
        struct world;

        static system const process_default;
        static system const unrestricted   ;
        static system const nix_default    ;
        static system const _644           ;

        unspecified-impl_specific public_data_members;
    }; // struct system

    object        /*const*/ object_access;
    child_process /*const*/ child_access ;
    system        /*const*/ system_access;
}
#endif // DOXYGEN_ONLY
; // struct access_privileges<win32>

template <typename Impl> using access_rights      = access_privileges<Impl>;
template <typename Impl> using access_permissions = access_privileges<Impl>;
template <typename Impl> using permissions        = access_privileges<Impl>;


////////////////////////////////////////////////////////////////////////////////
///
/// \class mapping
///
/// \brief Flags for specifying access modes and usage patterns/hints when
/// creating mapping objects.
///
////////////////////////////////////////////////////////////////////////////////

template <typename Impl>
struct mapping
#ifdef DOXYGEN_ONLY
{
    enum struct share_mode
    {
        shared, ///< Enable IPC access to the mapped region
        hidden  ///< Map as process-private (i.e. w/ COW semantics)
    };

    static mapping create ///< Factory function
    (
        flags_t    combined_handle_access_rights,
        share_mode share_mode
    );

    unspecified-impl_specific public_data_members;
}
#endif // DOXYGEN_ONLY
; // struct mapping

template <typename Impl> struct viewing;


////////////////////////////////////////////////////////////////////////////////
///
/// \class opening
///
/// \brief Flags for opening/creating "named" OS level objects (e.g. files and
/// shared memory mappings).
///
////////////////////////////////////////////////////////////////////////////////

template <typename Impl> struct access_pattern_optimisation_hints;
template <typename Impl> using system_hints = access_pattern_optimisation_hints<Impl>;

template <typename Impl>
struct opening
#ifdef DOXYGEN_ONLY
{
    /// Access-pattern optimisation hints
    struct access_pattern_optimisation_hints
    {
        enum flags
        {
            random_access,
            sequential_access,
            avoid_caching,
            temporary
        };
    };
    using system_hints = access_pattern_optimisation_hints;

    /// Factory function
    static opening<Impl> create
    (
        flags_t handle_access_flags,
        open_policy,
        flags_t system_hints,
        flags_t on_construction_rights
    );

    /// Factory function
    static opening<Impl> create_for_opening_existing_files
    (
        flags_t handle_access_flags,
        flags_t system_hints
        bool    truncate,
    );

    unspecified-impl_specific public_data_members;
}
#endif // DOXYGEN_ONLY
; // struct opening

//------------------------------------------------------------------------------
} // namespace flags
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifndef DOXYGEN_ONLY
#include "boost/mmap/detail/impl_selection.hpp"
#include BOOST_MMAP_IMPL_INCLUDE( BOOST_PP_EMPTY, BOOST_PP_IDENTITY( /flags.hpp ) )
#endif // DOXYGEN_ONLY

#endif // flags_hpp
