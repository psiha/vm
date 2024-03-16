#include <psi/vm/vector.hpp>

#include <gtest/gtest.h>

#include <complex>
#include <cstdint>
#include <filesystem>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

TEST( vector, playground )
{
    std::filesystem::path const test_vec{ "test_vec" };
    std::filesystem::remove( test_vec );
    {
        psi::vm::vector< double, std::uint16_t > vec;
        vec.map_file( test_vec.c_str() );
        EXPECT_EQ( vec.size(), 0 );
        vec.append_range({ 3.14, 0.14, 0.04 });
        EXPECT_EQ( vec.size(), 3 );
        EXPECT_EQ( vec[ 0 ], 3.14 );
        EXPECT_EQ( vec[ 1 ], 0.14 );
        EXPECT_EQ( vec[ 2 ], 0.04 );
    }
    {
        psi::vm::vector< double, std::uint16_t > vec;
        vec.map_file( test_vec.c_str() );
        EXPECT_EQ( vec.size(), 3 );
        EXPECT_EQ( vec[ 0 ], 3.14 );
        EXPECT_EQ( vec[ 1 ], 0.14 );
        EXPECT_EQ( vec[ 2 ], 0.04 );
    }
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
