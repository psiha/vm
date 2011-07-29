////////////////////////////////////////////////////////////////////////////////
///
/// \file flags.hpp
/// ---------------
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

#ifndef flags_hpp__0F422517_D9AA_4E3F_B3E4_B139021D068E
#define flags_hpp__0F422517_D9AA_4E3F_B3E4_B139021D068E
#pragma once
//------------------------------------------------------------------------------
#include "boost/assert.hpp"
#include "boost/noncopyable.hpp"

#include "fcntl.h"
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

struct posix_file_flags
{
    struct handle_access_rights
    {
        static unsigned int const read   ;
        static unsigned int const write  ;
        static unsigned int const execute;
    };

    struct share_mode
    {
        static unsigned int const none  ;
        static unsigned int const read  ;
        static unsigned int const write ;
        static unsigned int const remove;
    };

    struct open_policy
    {
        enum value_type
        {
            create_new                      = O_CREAT | O_EXCL ,
            create_new_or_truncate_existing = O_CREAT | O_TRUNC,
            open_existing                   = 0                ,
            open_or_create                  = O_CREAT          ,
            open_and_truncate_existing      = O_TRUNC
        };
    };
    typedef open_policy::value_type open_policy_t;

    struct system_hints
    {
        static unsigned int const random_access    ;
        static unsigned int const sequential_access;
        static unsigned int const non_cached       ;
        static unsigned int const delete_on_close  ;
        static unsigned int const temporary        ;
    };

    struct on_construction_rights
    {
        static unsigned int const read   ;
        static unsigned int const write  ;
        static unsigned int const execute;
    };

    static posix_file_flags create
    (
        unsigned int handle_access_flags   ,
        unsigned int share_mode            ,
        open_policy_t                      ,
        unsigned int system_hints          ,
        unsigned int on_construction_rights
    );

    static posix_file_flags create_for_opening_existing_files
    (
        unsigned int handle_access_flags,
        unsigned int share_mode         ,
        bool         truncate           ,
        unsigned int system_hints
    );

    int oflag;
    int pmode;
};

//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    #include "flags.inl"
#endif // BOOST_MMAP_HEADER_ONLY

#endif // flags_hpp
