# Optional C++20 named module support for psi::vm (opt-in, off by default).
#
# Adds a second target, `psi_vm_module`, exporting every psi::vm public header as `import psi.vm;`
# instead of textual `#include`. It is purely additive: the existing `psi_vm` target, its sources,
# and its own PCH (src/pch.hpp) are untouched, so header-only consumers that vendor this repo's
# headers directly (as opposed to building this CMakeLists.txt) are unaffected regardless of
# whether this option exists, let alone whether it is on.
#
# Shape: one interface unit whose global module fragment carries the STL + the two macro-only psiha
# deps this library's own headers use (boost/assert.hpp, boost/config_ex.hpp) textually - modules
# never export macros, so BOOST_ASSERT etc. stay visible only within this unit's own translation;
# any consumer wanting BOOST_ASSERT after `import psi.vm;` still includes boost/assert.hpp itself,
# same as any project consuming Boost directly. The purview (`export extern "C++"`) carries every
# real psi::vm header via the same file(GLOB_RECURSE) vm.cmake already uses for its library sources,
# so newly added headers are picked up automatically without editing this file.
#
# Toolchain: only clang-cl and the GNU-driver clang have the module scan/BMI rules this needs.
# CMake ships those rules only for the GNU-driver clang; clang-cl needs them injected (the four
# CMAKE_CXX_* variables below), matching farseerdev/rama's own cmake/modules.cmake, which proved
# this shape on clang-cl in production use. MSVC proper and GCC are left alone entirely - the
# option silently has no effect there (a warning, not a hard error, since CI's default matrix
# leaves this off and a user turning it on there deserves a clear reason nothing happened).

option( PSI_VM_MODULE "Build psi.vm as a C++20 named module (opt-in; requires clang-cl or clang)" OFF )

if ( PSI_VM_MODULE )
    if ( NOT ( CMAKE_CXX_COMPILER_ID MATCHES Clang ) )
        message( WARNING "PSI_VM_MODULE requires clang-cl or clang; ignored on ${CMAKE_CXX_COMPILER_ID}." )
    else()
        if ( CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" )
            # CMake's Clang-CXX module rules are written only for the GNU-driver clang; clang-cl
            # accepts the exact same modmap/BMI flags via its /clang: passthrough, so inject them.
            cmake_path( GET CMAKE_CXX_COMPILER PARENT_PATH _psi_vm_llvm_bin )
            find_program( PSI_VM_CLANG_SCAN_DEPS clang-scan-deps HINTS "${_psi_vm_llvm_bin}" REQUIRED )
            string( CONCAT CMAKE_CXX_SCANDEP_SOURCE
                "\"${PSI_VM_CLANG_SCAN_DEPS}\" -format=p1689 -- <CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS>"
                " /TP <SOURCE> /c /Fo<OBJECT> /clang:-MT /clang:<DYNDEP_FILE> /clang:-MD /clang:-MF /clang:<DEP_FILE>"
                " > <DYNDEP_FILE>.tmp && \"${CMAKE_COMMAND}\" -E rename <DYNDEP_FILE>.tmp <DYNDEP_FILE>" )
            set( CMAKE_CXX_MODULE_MAP_FORMAT clang )
            set( CMAKE_CXX_MODULE_MAP_FLAG "@<MODULE_MAP_FILE>" )
            set( CMAKE_CXX_COMPILE_BMI "<CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS> /clang:-o<OBJECT> /clang:--precompile /TP <SOURCE>" )
        endif()

        file( GLOB_RECURSE psi_vm_module_headers RELATIVE "${CMAKE_CURRENT_LIST_DIR}/include" "${CMAKE_CURRENT_LIST_DIR}/include/*.hpp" )
        # Optional allocator-backend adapters each require their own external library
        # (jemalloc/tcmalloc/mimalloc) present only when that specific backend is enabled - a
        # condition the module purview, built once regardless of backend choice, cannot express.
        # A consumer wanting one of these still includes it directly after importing psi.vm,
        # same as today.
        list( FILTER psi_vm_module_headers EXCLUDE REGEX "allocators/(jemalloc|tcmalloc|mimalloc|mi_heap|mi_scoped_heap)\\.hpp$" )
        # containers/allocator.hpp's #include "mapping/mapping.hpp" only resolves with an include
        # path this repo's own build never sets up (verified: it fails to compile standalone with
        # nothing beyond the usual include/ root, module or not) - a pre-existing defect unrelated
        # to module support, tracked separately rather than fixed here.
        list( FILTER psi_vm_module_headers EXCLUDE REGEX "containers/allocator\\.hpp$" )
        # guarded_operation.hpp is included by nothing else in the tree and fails to compile in
        # isolation regardless of modules (a pre-existing mismatched `<...hpp"` include delimiter) -
        # same category as the previous exclusion, tracked separately.
        list( FILTER psi_vm_module_headers EXCLUDE REGEX "mapped_view/guarded_operation\\.hpp$" )
        # implementations.hpp is included by nothing else in the tree; its tag-type `struct win32{}`
        # (etc.) collides with the real `inline namespace win32` the platform implementation files
        # declare in the same psi::vm scope - the two are never included together in any real,
        # working TU today, only in this purview's unconditional flatten.
        list( FILTER psi_vm_module_headers EXCLUDE REGEX "(^|/)implementations\\.hpp$" )
        # mappable_objects/shared_memory has zero test coverage (grepped: no test/*.cpp references
        # it at all) and its headers fail to compile together (an ambiguous `detail` reference, then
        # a cascade of missing symbols once that's worked around) even after every platform/orphan
        # exclusion above - a genuinely untested, less mature corner, not something to debug as a
        # side effect of adding module support. Left out of v1; the rest of psi::vm (containers,
        # mapped_view, mappable_objects/file, handles, flags, error) is unaffected.
        list( FILTER psi_vm_module_headers EXCLUDE REGEX "mappable_objects/shared_memory" )
        # Platform-flavoured headers exist on every platform's checkout but only build on their own
        # (vm.cmake applies the identical exclusion to its own .cpp sources via excluded_impl) - the
        # purview, generated once per configure, needs the same split.
        if ( WIN32 )
            list( FILTER psi_vm_module_headers EXCLUDE REGEX "(posix|android)" )
        else()
            list( FILTER psi_vm_module_headers EXCLUDE REGEX "(win32|(^|[./])nt\\.hpp$)" )
            if ( NOT CMAKE_SYSTEM_NAME MATCHES "Android" )
                list( FILTER psi_vm_module_headers EXCLUDE REGEX "android" )
            endif()
        endif()
        set( _psi_vm_module_purview "${CMAKE_CURRENT_BINARY_DIR}/psi_vm_module_purview.hpp" )
        set( _psi_vm_module_content "// Generated by module.cmake - every psi::vm public header, cumulative.\n" )
        foreach( h ${psi_vm_module_headers} )
            string( APPEND _psi_vm_module_content "#include <${h}>\n" )
        endforeach()
        file( WRITE "${_psi_vm_module_purview}" "${_psi_vm_module_content}" )

        set( _psi_vm_module_unit "${CMAKE_CURRENT_BINARY_DIR}/psi_vm_module.cppm" )
        file( WRITE "${_psi_vm_module_unit}" "\
module;
#if defined( __x86_64__ ) || defined( _M_X64 )
#include <immintrin.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef ERROR
#endif
#include <algorithm>
#include <array>
#include <std_fix/bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <tuple>
#include <utility>
#include <version>
#include <boost/assert.hpp>
#include <boost/config_ex.hpp>
export module psi.vm;
export extern \"C++\"
{
#include \"${_psi_vm_module_purview}\"
}
" )

        add_library( psi_vm_module STATIC )
        # vm.cmake unsets CMAKE_CXX_STANDARD for clang-cl (it passes the dialect directly via
        # /clang:-std=gnu++2c instead), but CMake's own module scanning requires a target-level
        # cxx_std_NN feature to know which standard it is scanning against.
        target_compile_features( psi_vm_module PUBLIC cxx_std_23 )
        target_sources( psi_vm_module PUBLIC FILE_SET CXX_MODULES BASE_DIRS "${CMAKE_CURRENT_BINARY_DIR}" FILES "${_psi_vm_module_unit}" )
        # Not linked against psi::vm here: that target's PCH is PUBLIC, and CMake's cross-target
        # module synthesis (the auto-generated "@synth_N" scan target for a FILE_SET reached via a
        # link edge) does not honour a DISABLE_PRECOMPILE_HEADERS override applied to this target -
        # it force-includes psi_vm's PCH into the synthesized scan anyway, colliding with this unit's
        # own, already-equivalent GMF content. Declarations only need the same include dirs psi_vm
        # itself uses; a consumer wanting the real out-of-line definitions (allocation.cpp, the
        # throw_* helpers, ...) links psi::vm alongside psi_vm_module explicitly, same as it would
        # for any header-declares/library-defines split.
        target_include_directories( psi_vm_module PUBLIC
            "${CMAKE_CURRENT_LIST_DIR}/include"
            "${build_SOURCE_DIR}/include"
            "${config_ex_SOURCE_DIR}/include"
            "${err_SOURCE_DIR}/include"
            "${std_fix_SOURCE_DIR}/include"
        )
        target_link_libraries( psi_vm_module PUBLIC
            Boost::container Boost::core Boost::assert Boost::integer
            Boost::move Boost::preprocessor Boost::stl_interfaces Boost::winapi Boost::utility
        )
        target_compile_options( psi_vm_module PRIVATE
            $<$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>:/clang:-std=gnu++2c /clang:-Wno-include-angled-in-module-purview /clang:-Wno-reserved-module-identifier>
            $<$<NOT:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>:-Wno-include-angled-in-module-purview -Wno-reserved-module-identifier>
        )
    endif()
endif()
