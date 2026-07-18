#include <psi/vm/align.hpp>
#include <psi/vm/allocation.hpp>
#include <psi/vm/mappable_objects/file/file.hpp>
#include <psi/vm/mappable_objects/file/utility.hpp>
#include <psi/vm/mapped_view/mapped_view.hpp>
#include <psi/vm/mapped_view/ops.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
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

    mapping make_rw_mapping( char const * const file_name, std::size_t const file_size )
    {
        auto file{ create_file( file_name, create_rw_file_flags( flags::named_object_construction_policy::open_or_create ) ) };
        EXPECT_TRUE( set_size( file, file_size )() );
        using ap = flags::access_privileges;
        return create_mapping
        (
            std::move( file ),
            ap::object{ ap::readwrite },
            ap::child_process::does_not_inherit,
            flags::mapping::share_mode::shared,
            file_size
        );
    }
} // anonymous namespace

TEST( reserving_mapped_view, base_address_stability_across_growth )
{
    auto  constexpr file_name  { "reserving_view.bin" };
    std::size_t constexpr step { reserve_granularity };
    std::size_t constexpr steps{ 24 };
    std::size_t constexpr file_size{ step * steps };
    test_file cleanup{ file_name };

    auto mapping{ make_rw_mapping( file_name, file_size ) };
    ASSERT_TRUE( mapping );

    auto view{ reserving_mapped_view::map( mapping, file_size, step )() };
    ASSERT_TRUE( view );
    ASSERT_EQ( view->size(), step );
    ASSERT_EQ( view->reservation_size(), align_up( file_size, reserve_granularity ) );

    auto * const original_base{ view->data() };
    ASSERT_NE( original_base, nullptr );

    // Write a recognizable pattern through a pointer captured before any growth.
    auto * const persistent_pointer{ reinterpret_cast<std::uint64_t *>( original_base ) };
    *persistent_pointer = 0x0123456789abcdefULL;

    for ( std::size_t target_step{ 2 }; target_step <= steps; ++target_step )
    {
        auto const target_size{ step * target_step };
        ASSERT_TRUE( view->expand( target_size, mapping )() ) << "growth step " << target_step;
        ASSERT_EQ( view->data(), original_base ) << "base address changed at step " << target_step;
        ASSERT_EQ( view->size(), target_size );
        // The tail is writable and zero-filled (fresh file pages).
        auto * const tail{ view->data() + target_size - step };
        EXPECT_EQ( tail[ 0 ], std::byte{ 0 } );
        std::memset( tail, static_cast<int>( target_step ), step );
    }

    // The pre-growth pointer still reads the original data.
    EXPECT_EQ( *persistent_pointer, 0x0123456789abcdefULL );
    for ( std::size_t target_step{ 2 }; target_step <= steps; ++target_step )
    {
        EXPECT_EQ( view->data()[ step * target_step - 1 ], std::byte( static_cast<unsigned char>( target_step ) ) );
    }
}

TEST( reserving_mapped_view, reopened_file_contents_and_stability )
{
    auto constexpr file_name{ "reserving_view_reopen.bin" };
    std::size_t constexpr size{ reserve_granularity * 4 };
    test_file cleanup{ file_name };

    {
        auto mapping{ make_rw_mapping( file_name, size ) };
        ASSERT_TRUE( mapping );
        auto view{ reserving_mapped_view::map( mapping, size, size )() };
        ASSERT_TRUE( view );
        std::memset( view->data(), 0x5a, size );
        ASSERT_TRUE( flush_blocking( *view, mapping.underlying_file() )() );
    }

    auto mapping{ make_rw_mapping( file_name, size ) };
    ASSERT_TRUE( mapping );

    // Start with a pure reservation (zero initial size), grow over the reopened file.
    auto view{ reserving_mapped_view::map( mapping, size * 2 )() };
    ASSERT_TRUE( view );
    ASSERT_EQ( view->size(), 0 );
    auto * const base{ view->data() };
    ASSERT_NE( base, nullptr );

    ASSERT_TRUE( view->expand( size / 2, mapping )() );
    ASSERT_EQ( view->data(), base );
    ASSERT_TRUE( view->expand( size, mapping )() );
    ASSERT_EQ( view->data(), base );

    for ( std::size_t i{ 0 }; i < size; i += reserve_granularity )
    {
        ASSERT_EQ( view->data()[ i ], std::byte{ 0x5a } ) << "offset " << i;
    }
}

TEST( reserving_mapped_view, exhaustion_is_a_clean_failure )
{
    auto constexpr file_name{ "reserving_view_exhaust.bin" };
    std::size_t constexpr reservation{ reserve_granularity * 2 };
    std::size_t constexpr file_size  { reserve_granularity * 8 };
    test_file cleanup{ file_name };

    auto mapping{ make_rw_mapping( file_name, file_size ) };
    ASSERT_TRUE( mapping );

    auto view{ reserving_mapped_view::map( mapping, reservation, reserve_granularity )() };
    ASSERT_TRUE( view );
    auto * const base{ view->data() };
    base[ 0 ] = std::byte{ 0x77 };

    // Within the reservation: succeeds, base stable.
    ASSERT_TRUE( view->expand( reservation, mapping )() );
    ASSERT_EQ( view->data(), base );

    // Beyond the reservation: fails cleanly, existing range untouched.
    auto const overgrown{ view->expand( reservation + reserve_granularity, mapping )() };
    ASSERT_FALSE( overgrown );
    ASSERT_EQ( view->data(), base        );
    ASSERT_EQ( view->size(), reservation );
    EXPECT_EQ( base[ 0 ], std::byte{ 0x77 } );

    // Shrink keeps the base; re-expansion within the reservation still works.
    view->shrink( reserve_granularity );
    ASSERT_EQ( view->size(), reserve_granularity );
    ASSERT_TRUE( view->expand( reservation, mapping )() );
    ASSERT_EQ( view->data(), base );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
