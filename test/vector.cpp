// TODO create template tests to test all (three) implementations
#include <psi/vm/containers/fc_vector.hpp>
#include <psi/vm/containers/tr_vector.hpp>

#include <gtest/gtest.h>

#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

TEST(vector_test, construction) {
    tr_vector<int> vec1;  // Default constructor
    EXPECT_TRUE(vec1.empty());

    tr_vector<int> vec2(5, 42);  // Constructor with size and value
    EXPECT_EQ(vec2.size(), 5);
    EXPECT_EQ(vec2[0], 42);

    tr_vector<int> vec3{1, 2, 3, 4, 5};  // Initializer list constructor
    EXPECT_EQ(vec3.size(), 5);
    EXPECT_EQ(vec3[4], 5);

    tr_vector<int> vec4(vec3.begin(), vec3.end());  // Range constructor
    EXPECT_EQ(vec4, vec3);
}


TEST(vector_test, element_access) {
    tr_vector<int> vec{10, 20, 30, 40};

    EXPECT_EQ(vec[2], 30);           // operator[]
    EXPECT_EQ(vec.at(3), 40);        // .at()
#if defined( __APPLE__ /*libunwind: malformed __unwind_info at 0x102558A6C bad second level page*/ ) || ( defined( __linux__ ) && !defined( NDEBUG ) /*dubious asan new-free and exception type mismatch*/ )
    // TODO :wat:
#else
    EXPECT_THROW( std::ignore = vec.at( 10 ), std::out_of_range );  // .at() with invalid index
#endif

    EXPECT_EQ(vec.front(), 10);      // .front()
    EXPECT_EQ(vec.back(), 40);       // .back()
}


TEST(vector_test, modifiers) {
    using test_str_t = std::conditional_t<is_trivially_moveable<std::string>, std::string, std::string_view>;
    tr_vector<test_str_t> vec;

    // Test push_back
    vec.emplace_back("1");
    vec.emplace_back("2");
    EXPECT_EQ(vec.size(), 2);
    EXPECT_EQ(vec[1], "2");

    // Test emplace_back
    vec.emplace_back("3");
    EXPECT_EQ(vec.size(), 3);
    EXPECT_EQ(vec[2], "3");

    // Test pop_back
    vec.pop_back();
    EXPECT_EQ(vec.size(), 2);
    EXPECT_EQ(vec.back(), "2");

    // Test insert
    std::string_view const sbo_overflower{ "01234567898765432100123456789876543210" };
    vec.emplace( vec.end  () , sbo_overflower );
    vec.emplace( vec.begin() , "0" );
    vec.emplace( vec.nth( 3 ), "3" );
    EXPECT_EQ( vec.front(), "0" );
    EXPECT_EQ( vec[ 1 ]   , "1" );
    EXPECT_EQ( vec[ 2 ]   , "2" );
    EXPECT_EQ( vec[ 3 ]   , "3" );
    EXPECT_EQ( vec[ 4 ]   , sbo_overflower );
    EXPECT_EQ( vec.size(), 5 );

    // Test erase
    vec.erase(vec.begin());
    EXPECT_EQ(vec.front(), "1");
    EXPECT_EQ(vec.size(), 4);

    // Test clear
    vec.clear();
    EXPECT_TRUE(vec.empty());
}


TEST(vector_test, capacity) {
    tr_vector<int> vec;
    EXPECT_TRUE(vec.empty());

    vec.resize(10, 42);  // Resize to larger size
    EXPECT_EQ(vec.size(), 10);
    EXPECT_EQ(vec[5], 42);

    vec.resize(5);  // Resize to smaller size
    EXPECT_EQ(vec.size(), 5);

    vec.shrink_to_fit();  // Shrink to fit (no testable behavior, but call it)
    EXPECT_GE(vec.capacity(), vec.size());
}


TEST(vector_test, range_support) {
    auto range = std::views::iota(1, 6);  // Range [1, 2, 3, 4, 5]
    tr_vector<int> vec(range.begin(), range.end());
    EXPECT_EQ(vec.size(), 5);
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[4], 5);
    vec.append_range({ 321, 654, 78, 0, 9 });
    EXPECT_EQ(vec[7], 78);
}


TEST(vector_test, move_semantics) {
    tr_vector<int> vec1{1, 2, 3, 4, 5};
    tr_vector<int> vec2(std::move(vec1));  // Move constructor

    EXPECT_TRUE(vec1.empty());
    EXPECT_EQ(vec2.size(), 5);
    EXPECT_EQ(vec2[0], 1);

    tr_vector<int> vec3;
    vec3 = std::move(vec2);  // Move assignment
    EXPECT_TRUE(vec2.empty());
    EXPECT_EQ(vec3.size(), 5);
    EXPECT_EQ(vec3[0], 1);
}


TEST(vector_test, iterators) {
    tr_vector<int> vec{10, 20, 30, 40};

    // Validate iterator traversal
    int sum = 0;
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        sum += *it;
    }
    EXPECT_EQ(sum, 100);

    // Test reverse iterators
    tr_vector<int> reversed(vec.rbegin(), vec.rend());
    EXPECT_EQ(reversed[0], 40);
    EXPECT_EQ(reversed[3], 10);

    // Const iterators
    tr_vector<int> const const_vec{ 1, 2, 3 };
    EXPECT_EQ(*const_vec.cbegin(), 1);
}


TEST(vector_test, edge_cases) {
    tr_vector<int> vec1;

    // Large vector test (memory constraints permitting)
#if 0 // TODO optional size_type overflow handling
    try {
        tr_vector<int> vec2(std::numeric_limits<size_t>::max() / 2);
        ADD_FAILURE() << "Expected bad_alloc exception for large vector";
    } catch ( std::bad_alloc const & ) {
        SUCCEED();
    } catch (...) {
        ADD_FAILURE() << "Unexpected exception type for large vector";
    }
#endif

    // Test with non-trivial types
    struct non_trivial {
        int value;
        non_trivial(int v) : value(v) {}
        ~non_trivial() { value = -1; }
    };

    fc_vector<non_trivial, 2> vec3;
    vec3.emplace_back(42);
    EXPECT_EQ(vec3[0].value, 42);
}


TEST(vector_test, comparison) {
    tr_vector<int> vec1{1, 2, 3};
    tr_vector<int> vec2{1, 2, 3};
    tr_vector<int> vec3{1, 2, 4};

    EXPECT_TRUE (vec1 == vec2);
    EXPECT_TRUE (vec1 != vec3);
    EXPECT_TRUE (vec1 <  vec3);
    EXPECT_FALSE(vec3 <  vec1);
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
