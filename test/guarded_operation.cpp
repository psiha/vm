// guarded_operation.hpp had no test coverage before this file (and no other header in the tree
// included it - confirmed while surveying every header for module-purview inclusion) and its
// template signature named a type, basic_mapped_span<Element>, that no longer exists anywhere in
// this codebase (mapped_span/read_only_mapped_span are plain byte spans, not Element-generic).
// This test exercises the fixed, generic-over-View signature against a real mapping, on the
// normal (non-faulting) path the function's own body takes for every argument that doesn't raise
// an access violation.
//
// The exceptional path (a genuine EXCEPTION_IN_PAGE_ERROR / SIGSEGV+SIGBUS caught mid-access,
// which is what this function exists for) is not exercised here: reliably provoking one needs an
// environment where the OS itself fails to satisfy a page fault against already-mapped memory
// (the header's own example: a network share that drops mid-read) - not something a portable
// unit test can manufacture safely on demand.
#include <psi/vm/allocation.hpp>
#include <psi/vm/mappable_objects/file/file.hpp>
#include <psi/vm/mappable_objects/file/utility.hpp>
#include <psi/vm/mapped_view/guarded_operation.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

namespace
{
    struct test_file
    {
        char const * const name;
        ~test_file() noexcept { std::error_code ec; std::filesystem::remove( name, ec ); }
    };
} // anonymous namespace

TEST( GuardedOperation, NonFaultingReadReturnsOperationResult )
{
    auto constexpr file_name{ "guarded_operation_read.bin" };
    std::size_t constexpr file_size{ 4096 };
    test_file cleanup{ file_name };

    auto file{ create_file( file_name, create_rw_file_flags( flags::named_object_construction_policy::open_or_create ) ) };
    ASSERT_TRUE( set_size( file, file_size )() );
    using ap = flags::access_privileges;
    auto mapping
    {
        create_mapping
        (
            std::move( file ),
            ap::object{ ap::readwrite },
            ap::child_process::does_not_inherit,
            flags::mapping::share_mode::shared,
            file_size
        )
    };
    ASSERT_TRUE( mapping );

    mapped_view const view{ mapping, 0, file_size };
    ASSERT_TRUE( view );

    bool error_handler_called{ false };
    auto const result
    {
        guarded_operation
        (
            mapped_span{ view },
            []( mapped_span const s ) noexcept { return s.size(); },
            [ & ]( void const * ) noexcept { error_handler_called = true; return std::size_t{ 0 }; }
        )
    };

    EXPECT_EQ( result, file_size );
    EXPECT_FALSE( error_handler_called );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
