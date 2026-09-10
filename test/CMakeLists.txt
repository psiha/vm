# psi::vm tests

# Fix ODR violations and compiler and linker option mismatches: add GTest after everything is already setup.
CPMAddPackage( "gh:google/googletest@1.15.2" )

include( GoogleTest )
if ( MSVC AND NOT CLANG_CL )
    target_compile_options( gtest PRIVATE /fp:precise )
elseif ( CLANG_CL )
    # GTest's value printers convert char8_t -> char32_t, which clang-cl flags
    # as -Wcharacter-conversion; with the toolchain's -Werror that fails gtest.
    # /fp:precise alone leaves the complex-arithmetic range clang-cl inherited
    # from the project's global -ffast-math ("basic") in place, which clang
    # then flags (as an error, under -WX) for silently overriding it back to
    # "full" -- so the gcc-style flags need the explicit /clang: passthrough
    # to actually turn fast-math off for this target.
    target_compile_options( gtest PRIVATE
        -Wno-character-conversion
        /clang:-fno-fast-math
        /clang:-fcomplex-arithmetic=full
        /clang:-Wno-error=overriding-option
    )
else()
    target_compile_options( gtest PRIVATE -fno-fast-math )
endif()

# The b+tree benchmark can compare against absl::btree_set - the closest
# shipping container to this one (it also redistributes into a sibling before
# splitting).  Off by default: it pulls in and builds abseil.
option( PSI_VM_BENCH_ABSL "b+tree benchmark: build the absl::btree_set comparison arm" OFF )
if ( PSI_VM_BENCH_ABSL )
    # psi.build's MSVC toolchain defines __GNUC__ globally (clang-cl does
    # support the GNU-style extensions) - abseil reads that as "GNU platform"
    # and tries to build its libsupc++ demangler, which wants cxxabi.h.  Drop
    # it for abseil's subtree only; a subdirectory inherits the directory
    # property as it stands at the add_subdirectory() call.
    get_directory_property( psi_vm_saved_defs COMPILE_DEFINITIONS )
    set( psi_vm_absl_defs ${psi_vm_saved_defs} )
    list( REMOVE_ITEM psi_vm_absl_defs __GNUC__ )
    set_directory_properties( PROPERTIES COMPILE_DEFINITIONS "${psi_vm_absl_defs}" )

    CPMAddPackage(
        NAME               abseil-cpp
        GITHUB_REPOSITORY  abseil/abseil-cpp
        GIT_TAG            20250127.0
        OPTIONS
            "ABSL_PROPAGATE_CXX_STD ON"
            "ABSL_ENABLE_INSTALL OFF"
            "ABSL_BUILD_TESTING OFF"
            "BUILD_TESTING OFF"
    )

    set_directory_properties( PROPERTIES COMPILE_DEFINITIONS "${psi_vm_saved_defs}" )
endif()

set( vm_test_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/b+tree.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/b+tree_space.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/cow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/strided_view.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/flat_map.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/flat_set.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/handle_release.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/lookup_threshold.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/reserving_mapped_view.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/small_vector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/strided_vector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vector_storage.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vm_vector.cpp"
)

add_executable( vm_unit_tests EXCLUDE_FROM_ALL ${vm_test_sources} )
target_link_libraries( vm_unit_tests PRIVATE GTest::gtest_main psi::vm )
if ( PSI_VM_BENCH_ABSL )
    target_link_libraries     ( vm_unit_tests PRIVATE absl::btree )
    target_compile_definitions( vm_unit_tests PRIVATE HAVE_ABSL=1 )
    # abseil is not built with this project's warning set
    if ( CLANG_CL OR CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
        target_compile_options( vm_unit_tests PRIVATE -Wno-error )
    endif()
endif()
if ( MSVC AND NOT CLANG_CL )
    target_compile_options( vm_unit_tests PRIVATE /bigobj )
endif()
if ( NOT MSVC OR CLANG_CL ) #:wat: msvc fails to find the pch file in release builds!?
	target_precompile_headers( vm_unit_tests REUSE_FROM psi_vm )
endif()

set_target_properties(
    vm_unit_tests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test"
)

PSI_target_fix_debug_symbols_for_osx_lto( vm_unit_tests )

add_test( "vm_unit_tests" vm_unit_tests )

# Codegen comparison for small_vector layouts (not a test — for disassembly inspection)
add_library( small_vector_codegen OBJECT EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_SOURCE_DIR}/small_vector_codegen.cpp"
)
target_link_libraries( small_vector_codegen PRIVATE psi::vm )
if ( NOT MSVC OR CLANG_CL )
    target_precompile_headers( small_vector_codegen REUSE_FROM psi_vm )
endif()
