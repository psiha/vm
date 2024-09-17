////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2023 - 2024.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
/// (See accompanying file LICENSE_1_0.txt or copy at
/// http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
#pragma once

#ifdef _WIN32
#include <Windows.h>
#elif defined( __EMSCRIPTEN__ )
#else
#include <sys/mman.h>
#endif // OS

#include <psi/vm/span.hpp>

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

#ifdef _MSC_VER
#   pragma warning( push )
#   pragma warning( disable : 5030 ) // Unrecognized attribute
#endif // _MSC_VER

#if defined( _WIN32 ) //////////////////////////////////////////////////////////

enum class allocation_type : std::uint32_t
{
    reserve = MEM_RESERVE,
    commit  = MEM_COMMIT
};

enum class reallocation_type : std::uint32_t { fixed = false, moveable };

inline std::uint16_t constexpr page_size          {  4 * 1024 };
inline std::uint16_t constexpr commit_granularity {  4 * 1024 };
inline std::uint32_t constexpr reserve_granularity{ 64 * 1024 };

#else // POSIX /////////////////////////////////////////////////////////////////

enum class allocation_type : int
{
    reserve = PROT_NONE,
    commit  = PROT_READ | PROT_WRITE
};

enum class reallocation_type
{
#ifdef __linux__
    fixed    = 0,
    moveable = MREMAP_MAYMOVE
#else
    fixed,
    moveable
#endif
};

#if ( defined( __APPLE__ ) && defined( __aarch64__ ) )
// https://bugzilla.redhat.com/show_bug.cgi?id=2001569
inline std::uint16_t constexpr page_size{ 16 * 1024 };
#else
// TODO AArch64 https://www.kernel.org/doc/html/next/arm64/memory.html
inline std::uint16_t constexpr page_size{ 4 * 1024 };
#endif
#if defined( PAGE_SIZE ) && !defined( __APPLE__ ) // not a constant under OSX
static_assert( page_size == PAGE_SIZE );
#endif

inline std::uint16_t constexpr  commit_granularity{ page_size };
inline std::uint16_t constexpr reserve_granularity{ page_size };

#endif // platform /////////////////////////////////////////////////////////////

[[ gnu::assume_aligned( reserve_granularity ), gnu::malloc, nodiscard ]] void * allocate(                 std::size_t & size ) noexcept;
[[ gnu::assume_aligned( reserve_granularity ), gnu::malloc, nodiscard ]] void * reserve (                 std::size_t & size ) noexcept;
                                                                         void   free    ( void * address, std::size_t   size ) noexcept;

[[ nodiscard ]] bool allocate_fixed( void * address, std::size_t size, allocation_type ) noexcept;


[[ nodiscard ]] bool commit  ( void * address, std::size_t size ) noexcept;
                void decommit( void * address, std::size_t size ) noexcept;

[[ nodiscard ]] inline bool commit  ( mapped_span const span ) noexcept { return commit  ( span.data(), span.size() ); }
                inline void decommit( mapped_span const span ) noexcept { return decommit( span.data(), span.size() ); }

struct [[ nodiscard ]] expand_result
{
    mapped_span new_span;
    enum method : std::uint8_t
    {
        moved,
        back_extended,
        front_extended
    } method;

    constexpr operator bool() const noexcept { return !new_span.empty(); }
}; // struct expand_result

expand_result expand_back
(
    mapped_span span,
    std::size_t required_size,
    std::size_t used_capacity,
    allocation_type,
    reallocation_type
) noexcept;

expand_result expand_front
(
    mapped_span span,
    std::size_t required_size,
    std::size_t used_capacity,
    allocation_type,
    reallocation_type
) noexcept;

expand_result expand
(
    mapped_span span,
    std::size_t required_size_for_end_expansion,
    std::size_t required_size_for_front_expansion,
    std::size_t used_capacity,
    allocation_type,
    reallocation_type
) noexcept;

#ifdef _MSC_VER
#   pragma warning( pop )
#endif // _MSC_VER

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
