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

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
