////////////////////////////////////////////////////////////////////////////////
///
/// \file file_allocated_size.cpp
/// -----------------------------
///
/// get_allocated_size() reports the storage the filesystem has allocated to a
/// file, which is less than get_size() for a file with holes.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include <psi/vm/mappable_objects/file/file.hpp>
#include <psi/vm/mappable_objects/file/utility.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>
#include <psi/vm/mapped_view/ops.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <utility>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

TEST( file_allocated_size, a_sparse_file_allocates_only_what_was_written )
{
    static constexpr auto file_name{ "file_allocated_size.bin" };
    std::size_t constexpr file_size{ std::size_t{ 64 } << 20 };
    struct cleanup_t { ~cleanup_t() noexcept { std::error_code ec; std::filesystem::remove( file_name, ec ); } } const cleanup;

    auto file{ create_file( file_name, create_rw_file_flags( flags::named_object_construction_policy::open_or_create ) ) };
    ASSERT_TRUE( file );
    EXPECT_EQ( get_allocated_size( file ), 0U );
    make_sparse( file );
    ASSERT_TRUE( set_size( file, file_size )() );
    EXPECT_EQ( get_size( file ), file_size );
    auto const holey{ get_allocated_size( file ) < file_size / 2 }; // not every filesystem keeps holes

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
    std::memset( view.data(), 0x5a, 4096 );
    ASSERT_TRUE( flush_blocking( mapped_span{ view }, mapping.underlying_file() )() );

    auto const allocated{ get_allocated_size( mapping.underlying_file() ) };
    EXPECT_GE( allocated, 4096U );
    EXPECT_LE( allocated, file_size + ( std::size_t{ 1 } << 20 ) ); // block rounding, never beyond the file
    if ( holey )
        EXPECT_LT( allocated, file_size / 4 );
    EXPECT_EQ( get_size( mapping.underlying_file() ), file_size ); // the length is untouched
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
