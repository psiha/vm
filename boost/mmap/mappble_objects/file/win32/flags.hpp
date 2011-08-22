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
#ifndef flags_hpp__77AE8A6F_0E93_433B_A1F2_531BBBB353FC
#define flags_hpp__77AE8A6F_0E93_433B_A1F2_531BBBB353FC
#pragma once
//------------------------------------------------------------------------------
#include "boost/assert.hpp"
#include "boost/noncopyable.hpp"
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------

template <typename Impl> struct file_flags;

// Implementation note:
//   Using structs with public members and factory functions to enable (almost)
// zero-overhead 'link-time' conversion to native flag formats and to allow the
// user to modify the created flags or create fully custom ones so that specific
// platform-dependent use-cases, not otherwise covered through the generic
// interface, can also be covered.
//                                            (10.10.2010.) (Domagoj Saric)

template <>
struct file_flags<win32>
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
            create_new                      = 1,
            create_new_or_truncate_existing = 2,
            open_existing                   = 3,
            open_or_create                  = 4,
            open_and_truncate_existing      = 5
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

    static file_flags<win32> create
    (
        unsigned int handle_access_flags   ,
        unsigned int share_mode            ,
        open_policy_t                      ,
        unsigned int system_hints          ,
        unsigned int on_construction_rights
    );

    static file_flags<win32> create_for_opening_existing_files
    (
        unsigned int handle_access_flags,
        unsigned int share_mode         ,
        bool         truncate           ,
        unsigned int system_hints
    );

    unsigned long desired_access      ;
    unsigned long share_mode          ;
    unsigned long creation_disposition;
    unsigned long flags_and_attributes;
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
