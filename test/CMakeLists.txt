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

set( vm_test_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/b+tree.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/cow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/strided_view.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/flat_map.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/flat_set.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/guarded_operation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/handle_release.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/reserving_mapped_view.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/small_vector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/strided_vector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vector.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vector_storage.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vm_vector.cpp"
)

add_executable( vm_unit_tests EXCLUDE_FROM_ALL ${vm_test_sources} )
target_link_libraries( vm_unit_tests PRIVATE GTest::gtest_main psi::vm )
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

# Module correctness smoke test (module.cmake, PSI_VM_MODULE) — only built when the option is on,
# so it adds nothing to the default (off) CI matrix.
if ( PSI_VM_MODULE AND TARGET psi_vm_module )
    add_executable( vm_module_smoke_test EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/module_smoke_test.cpp" )
    # psi_vm_module supplies the declarations (import psi.vm;); psi::vm supplies the real
    # out-of-line definitions those declarations point at (allocation.cpp, the throw_* helpers, ...).
    target_link_libraries( vm_module_smoke_test PRIVATE psi_vm_module psi::vm )
    set_target_properties( vm_module_smoke_test PROPERTIES CXX_SCAN_FOR_MODULES ON DISABLE_PRECOMPILE_HEADERS ON )
    add_test( "vm_module_smoke_test" vm_module_smoke_test )
endif()

# Per-subsystem module correctness smoke test: a consumer combining several of the submodules
# module.cmake generates (containers, mapping, mapped_view, file, shared_memory, containers.vm_backed)
# links without conflict even though none of them `import` one another.
if ( PSI_VM_MODULE AND TARGET psi_vm_module_containers )
    add_executable( vm_submodule_smoke_test EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/submodule_smoke_test.cpp" )
    target_link_libraries( vm_submodule_smoke_test PRIVATE
        psi_vm_module_containers
        psi_vm_module_mapping
        psi_vm_module_mapped_view
        psi_vm_module_file
        psi_vm_module_shared_memory
        psi_vm_module_containers_vm_backed
        psi::vm
    )
    set_target_properties( vm_submodule_smoke_test PROPERTIES CXX_SCAN_FOR_MODULES ON DISABLE_PRECOMPILE_HEADERS ON )
    add_test( "vm_submodule_smoke_test" vm_submodule_smoke_test )
endif()
