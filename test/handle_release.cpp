////////////////////////////////////////////////////////////////////////////////
///
/// \file handle_release.cpp
/// ------------------------
///
/// Tests that releasing an already-empty handle or mapped view issues no
/// syscall at all.
///
/// Destroying a default-constructed or moved-from object is an ordinary and
/// very frequent event - a single object owning both a mapping and a view (the
/// vm allocator, for one) produces one of each per destruction. Letting those
/// reach close()/munmap() costs a failing syscall every time and forces the
/// accompanying assertions to whitelist the failure, which masks genuine ones.
///
/// errno is used as the witness: a skipped syscall cannot modify it, whereas
/// close( -1 ) sets EBADF and munmap( nullptr, 0 ) sets EINVAL.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include <psi/vm/handles/handle.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>
#include <psi/vm/mapped_view/ops.hpp>

#include <gtest/gtest.h>

#include <cerrno>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace
{
    // Only the POSIX backend reports through errno; on Win32 the equivalent
    // channel is GetLastError() and the guard there is verified by the
    // CloseHandle() assertion no longer needing to tolerate a null handle.
#ifndef _WIN32
    auto constexpr errno_is_the_witness{ true };
#else
    auto constexpr errno_is_the_witness{ false };
#endif
} // anonymous namespace

TEST( handle_release, destroying_a_default_constructed_handle_makes_no_syscall )
{
    errno = 0;
    {
        handle const empty;
        EXPECT_FALSE( static_cast<bool>( empty ) );
    } // destructor runs here
    if constexpr ( errno_is_the_witness )
        EXPECT_EQ( errno, 0 ) << "destroying an empty handle issued a failing close()";
}

TEST( handle_release, destroying_a_moved_from_handle_makes_no_syscall )
{
    // A moved-from handle holds the invalid value: releasing it must be free.
    handle source;
    handle const sink{ std::move( source ) };
    errno = 0;
    {
        auto const moved_from{ std::move( source ) };
        (void)moved_from;
    }
    if constexpr ( errno_is_the_witness )
        EXPECT_EQ( errno, 0 ) << "destroying a moved-from handle issued a failing close()";
}

TEST( handle_release, closing_an_empty_handle_twice_makes_no_syscall )
{
    handle empty;
    errno = 0;
    empty.close();
    empty.close(); // idempotent: release() already nulled it
    if constexpr ( errno_is_the_witness )
        EXPECT_EQ( errno, 0 ) << "close() on an empty handle issued a failing close()";
}

TEST( handle_release, resetting_over_an_empty_handle_makes_no_syscall )
{
    handle empty;
    errno = 0;
    empty.reset( handle::invalid_value );
    if constexpr ( errno_is_the_witness )
        EXPECT_EQ( errno, 0 ) << "reset() over an empty handle issued a failing close()";
}

TEST( handle_release, unmapping_an_empty_view_makes_no_syscall )
{
    mapped_view empty;
    ASSERT_FALSE( static_cast<bool>( empty ) );
    errno = 0;
    empty.unmap();
    if constexpr ( errno_is_the_witness )
        EXPECT_EQ( errno, 0 ) << "unmapping an empty view issued a failing munmap()";
}

TEST( handle_release, destroying_a_default_constructed_view_makes_no_syscall )
{
    errno = 0;
    {
        mapped_view const empty;
    } // destructor runs here
    if constexpr ( errno_is_the_witness )
        EXPECT_EQ( errno, 0 ) << "destroying an empty view issued a failing munmap()";
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
