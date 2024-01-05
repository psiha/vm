# TESTS
include(GoogleTest)

# set(
#     vm_test_sources
# 
#     # Common
#     "${PROJECT_SOURCE_DIR}/test/pch.hpp"
# 
#     # Core tests
#     "${CMAKE_CURRENT_SOURCE_DIR}/vector_test.cpp"
# )
# 
# Creating executable test target
# add_executable(rama_unit_tests EXCLUDE_FROM_ALL ${rama_test_sources})
# target_link_libraries(rama_unit_tests database api GTest::gtest GTest::gtest_main Boost::boost Boost.Container )
# target_precompile_headers(rama_unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/test/pch.hpp")
# target_include_directories(
#     rama_unit_tests
#     PRIVATE
#         ${PROJECT_SOURCE_DIR}/src
#         ${PROJECT_SOURCE_DIR}/deps/gtest/googletest/include
#         ${PROJECT_SOURCE_DIR}/deps/range-v3/include
#         ${PROJECT_SOURCE_DIR}/deps/psiha/std_fix/include
# )
# set_target_properties(
#     rama_unit_tests
#     PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test"
#     COMMAND rama_unit_tests
# )
# 
# gtest_discover_tests(
#     rama_unit_tests
#     EXTRA_ARGS
#     --gtest_color=auto
#     --gtest_output=xml:${CMAKE_BINARY_DIR}/test/rama_unit_tests.xml
#     --gtest_catch_exceptions=0
#     DISCOVERY_TIMEOUT 120
#     PROPERTIES
#     TIMEOUT 120
# )
# 
# add_test("rama_unit_tests" rama_unit_tests)
