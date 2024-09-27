# psi::vm tests

# Fix ODR violations and compiler and linker option mismatches: add GTest after everything is already setup.
CPMAddPackage( "gh:google/googletest@1.15.2" )

include( GoogleTest )

set( vm_test_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/b+tree.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vector.cpp"
)

add_executable( vm_unit_tests EXCLUDE_FROM_ALL ${vm_test_sources} )
target_link_libraries( vm_unit_tests PRIVATE GTest::gtest_main psi::vm )
target_precompile_headers( vm_unit_tests REUSE_FROM psi_vm )

set_target_properties(
    vm_unit_tests
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test"
    COMMAND vm_unit_tests
)

gtest_discover_tests(
    vm_unit_tests
    EXTRA_ARGS
    --gtest_color=auto
    --gtest_output=xml:${CMAKE_BINARY_DIR}/test/vm_unit_tests.xml
    --gtest_catch_exceptions=1
    DISCOVERY_TIMEOUT 120
    PROPERTIES
    TIMEOUT 120
)

add_test( "vm_unit_tests" vm_unit_tests )
