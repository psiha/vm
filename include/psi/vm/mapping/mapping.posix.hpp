////////////////////////////////////////////////////////////////////////////////
///
/// \file mapping.hpp
/// -----------------
///
/// Copyright (c) Domagoj Saric 2011 - 2026.
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

#include <psi/vm/error/error.hpp>
#include <psi/vm/flags/mapping.posix.hpp>
#include <psi/vm/handles/handle.posix.hpp>

#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
PSI_VM_POSIX_INLINE
namespace posix
{
//------------------------------------------------------------------------------

struct [[ clang::trivial_abi ]] mapping
    :
    handle
{
    using const_handle = posix::handle::const_reference;
    using       handle = posix::handle::      reference;

    using const_reference = mapping const &;
    using       reference = mapping       &;

    static bool constexpr create_mapping_can_set_source_size = false;
    static bool constexpr supports_zero_sized_mappings       = true ; // simply because there is actually no intermediate mapping object
    static bool constexpr views_downsizeable                 = true ;

    constexpr mapping() noexcept = default;
    // Explicit move constructor: must clear source fields that operator bool()
    // checks (is_anonymous reads view_mapping_flags). A defaulted move would
    // copy but NOT clear them, leaving the source appearing "attached".
    constexpr mapping( mapping && source ) noexcept
        : vm::handle        { std::move( source )                            }
        , view_mapping_flags{ std::exchange( source.view_mapping_flags, {} ) }
        , maximum_size      { std::exchange( source.maximum_size      , {} ) }
#   ifdef __linux__
        , ephemeral_        { std::exchange( source.ephemeral_        , {} ) }
#   endif
    {}

    template <typename FileHandle>
    constexpr mapping( FileHandle && fd, flags::viewing const & view_mapping_flags_param, std::size_t const size ) noexcept
        : vm::handle{ std::forward<FileHandle>( fd ) }, view_mapping_flags{ view_mapping_flags_param }, maximum_size{ size } {}

    bool is_read_only() const noexcept
    {
        return
        (
            static_cast< std::uint32_t >( view_mapping_flags.protection ) &
            ( flags::access_privileges::write | flags::access_privileges::readwrite )
        ) == 0;
    }

    // is_file_based: true only for persistent on-disk files.
    // On Linux, excludes ephemeral fds (memfd_create for COW backing).
#ifdef __linux__
    // Mark this mapping as backed by an ephemeral (non-persistent) fd (e.g. memfd).
    void set_ephemeral() noexcept { ephemeral_ = true; }
    bool is_file_based() const noexcept { return has_fd() && !ephemeral_; }
#else
    bool is_file_based() const noexcept { return has_fd(); }
#endif

    // has_fd: true for any valid fd (including memfd). Used internally by
    // COW clone routing — dup + MAP_PRIVATE works for all fd-backed mappings.
    bool has_fd() const noexcept { return posix::handle::operator bool(); }

    bool is_anonymous() const noexcept { return view_mapping_flags.flags & MAP_ANONYMOUS; }

    posix::handle::      reference underlying_file()       noexcept { return *this; }
    posix::handle::const_reference underlying_file() const noexcept { return *this; }

    constexpr mapping & operator=( mapping && source ) noexcept
    {
        vm::handle::operator=( std::move( source ) );
        view_mapping_flags = std::exchange( source.view_mapping_flags, {} );
        maximum_size       = std::exchange( source.maximum_size      , {} );
#   ifdef __linux__
        ephemeral_         = std::exchange( source.ephemeral_        , {} );
#   endif
        return *this;
    }

    // override Handle operator bool to allow for anonymous mappings
    explicit operator bool() const noexcept { return posix::handle::operator bool() || is_anonymous(); }

    flags::viewing view_mapping_flags{};
    std  ::size_t  maximum_size      {};
#ifdef __linux__
    bool           ephemeral_        {}; // memfd: fd-backed but not on-disk (fits in size_t padding)
#endif
}; // struct mapping

// fwd declarations for file_handle functions which work for mappings as well ('everything is a file')
err::fallible_result<void, error> set_size( handle::      reference, std::uint64_t desired_size ) noexcept;
std::uint64_t                     get_size( handle::const_reference                             ) noexcept;

// (temporary) special overloads to handle anonymous mappings (until a dedicated mapping type sees the light of day)
err::fallible_result<void, error> set_size( mapping       &, std::size_t desired_size ) noexcept;
std::size_t                       get_size( mapping const &                           ) noexcept;

//------------------------------------------------------------------------------
} // namespace posix
//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
