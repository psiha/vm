////////////////////////////////////////////////////////////////////////////////
///
/// \file flags.hpp
/// ---------------
///
/// Copyright (c) Domagoj Saric 2010 - 2015.
///
///  Use, modification and distribution is subject to the Boost Software License, Version 1.0.
///  (See accompanying file LICENSE_1_0.txt or copy at
///  http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef flags_hpp__BFFC0541_21AC_4A80_A9EE_E0450B6D4D8A
#define flags_hpp__BFFC0541_21AC_4A80_A9EE_E0450B6D4D8A
#pragma once
//------------------------------------------------------------------------------
#include "../../detail/impl_selection.hpp"

#include BOOST_MMAP_IMPL_INCLUDE( BOOST_PP_EMPTY, BOOST_PP_IDENTITY( /mapping_flags.hpp ) )
#include BOOST_MMAP_IMPL_INCLUDE( BOOST_PP_EMPTY, BOOST_PP_IDENTITY( /open_flags.hpp    ) )
//------------------------------------------------------------------------------

#ifdef DOXYGEN_ONLY

// Implementation note:
//   Using structs with public members and factory functions to enable (almost)
// zero-overhead (w/ IPO & LTO) conversion to native flag formats and to allow the
// user to modify the created flags or create fully custom ones so that specific
// platform-dependent use-cases, not otherwise covered through the generic
// interface, can also be supported.
//                                            (10.10.2010.) (Domagoj Saric)

template <typename Impl>
struct file_mapping_flags
{
    struct handle_access_rights
    {
        enum values
        {
            read = PROT_READ    , ///< Allow reading from the mapped region
            write = PROT_WRITE  , ///< Allow writing to the mapped region
            execute = PROT_EXEC , ///< Allow executing code from the mapped region
            all = read | write | execute ///< Allow every possible access type
        };
    };

    struct share_mode
    {
        enum value_type
        {
            shared, ///< Enable IPC access to the mapped region
            hidden  ///< Map as process-private
        };
    };

    static file_mapping_flags<Impl> create ///< Factory function
    (
        flags_t                combined_handle_access_rights,
        share_mode::value_type share_mode
    );

    unspecified-impl_specific public_data_members;
};

template <Impl>
struct file_open_flags
{
    /// Object access rights
    struct handle_access_rights
    {
        enum values
        {
            read = 0x80000000L,    ///< Allow reading from the oppened file
            write = 0x40000000L,   ///< Allow writing to the oppened file
            both = read | write,   ///< Allow reading from and writing to the oppened file
            readwrite = both,   ///< Alias for both
            all = 0x10000000L   ///< Request all possible access rights
        };
    };

    /// Behaviour policies WRT (non)existing files
    struct open_policy
    {
        enum value_type
        {
            create_new = 1,   ///< Create a new file or fail if one already exists
            create_new_or_truncate_existing = 2, ///< Create a new file or truncate a possibly existing file (i.e. always-start-with-an-empty-file semantics)
            open_existing = 3,///< Open a file iff it already exists or fail otherwise
            open_or_create = 4,///< Open a file if it already exists or create it otherwise
            open_and_truncate_existing = 5///< Open a file, truncating it, iff it already exists or fail otherwise
        };
    };

    /// Access-pattern optimisation hints
    struct system_hints
    {
        enum values
        {
            random_access = 0x10000000,
            sequential_access = 0x08000000,
            avoid_caching = 0x20000000 | 0x80000000,
            temporary = 0x00000100 | 0x04000000
        };
    };

    /// File system rights
    struct on_construction_rights
    {
        enum values
        {
            read = 0x00000001,
            write = 0x00000080,
            execute = 0x00000001,
        };
    };

    /// Factory function
    static file_open_flags<Impl> create
    (
        flags_t handle_access_flags,
        open_policy::value_type,
        flags_t system_hints,
        flags_t on_construction_rights
    );

    /// Factory function
    static file_open_flags<Impl> create_for_opening_existing_files
    (
        flags_t handle_access_flags,
        bool    truncate,
        flags_t system_hints
    );

    unspecified-impl_specific public_data_members;
}; // struct file_open_flags

#endif // DOXYGEN_ONLY


#ifdef BOOST_MMAP_HEADER_ONLY
    #include "flags.inl"
#endif // BOOST_MMAP_HEADER_ONLY

#endif // flags_hpp
