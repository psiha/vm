#include <psi/vm/containers/vm_vector.hpp>

#include <gtest/gtest.h>

#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

TEST( vm_vector, anon_memory_backed )
{
    psi::vm::vm_vector<double, std::uint32_t> vec;
    vec.map_memory();
    EXPECT_EQ( vec.size(), 0 );
    vec.append_range({ 3.14, 0.14, 0.04 });
    EXPECT_EQ( vec.size(), 3 );
    EXPECT_EQ( vec[ 0 ], 3.14 );
    EXPECT_EQ( vec[ 1 ], 0.14 );
    EXPECT_EQ( vec[ 2 ], 0.04 );
    vec.grow_by( 12345678, default_init );
    // test growth (with 'probable' relocation) does not destroy contents
    EXPECT_EQ( vec[ 0 ], 3.14 );
    EXPECT_EQ( vec[ 1 ], 0.14 );
    EXPECT_EQ( vec[ 2 ], 0.04 );
}

TEST( vm_vector, file_backed )
{
    auto const test_vec{ "test.vec" };
    {
        psi::vm::vm_vector<double, std::uint16_t> vec;
        vec.map_file( test_vec, flags::named_object_construction_policy::create_new_or_truncate_existing );
        EXPECT_EQ( vec.size(), 0 );
        vec.append_range({ 3.14, 0.14, 0.04 });
        EXPECT_EQ( vec.size(), 3 );
        EXPECT_EQ( vec[ 0 ], 3.14 );
        EXPECT_EQ( vec[ 1 ], 0.14 );
        EXPECT_EQ( vec[ 2 ], 0.04 );
    }
    {
        psi::vm::vm_vector<double, std::uint16_t> vec;
        vec.map_file( test_vec, flags::named_object_construction_policy::open_existing );
        EXPECT_EQ( vec.size(), 3 );
        EXPECT_EQ( vec[ 0 ], 3.14 );
        EXPECT_EQ( vec[ 1 ], 0.14 );
        EXPECT_EQ( vec[ 2 ], 0.04 );
    }
}

// A whole-container flush has to keep working after the storage has grown past
// its initially mapped view: growth expands in place where it can, which on
// Win32 maps the extra pages over the trailing placeholder - i.e. as a second,
// adjacent view. The span stays contiguous to read and write through, but
// FlushViewOfFile() only accepts a range lying within a single view and fails
// with ERROR_INVALID_ADDRESS for anything crossing the boundary.
TEST( vm_vector, flush_after_in_place_growth )
{
    auto const test_vec{ "test_flush.vec" };
    psi::vm::vm_vector<std::uint32_t, std::uint32_t> vec;
    vec.map_file( test_vec, flags::named_object_construction_policy::create_new_or_truncate_existing );
    // Grow in small steps, well past any initial view size, flushing the whole
    // container every time.
    for ( unsigned i{ 0 }; i < 512; ++i )
    {
        vec.grow_by( 1024, default_init );
        auto result{ vec.flush_blocking()() };
        EXPECT_TRUE( result ) << "whole-container flush failed at growth step " << i;
    }
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
