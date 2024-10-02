cmake_minimum_required( VERSION 3.27.0 )

project( vm )

###################
## Deps
###################

# Download CPM.cmake

file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/get_cpm.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/get_cpm.cmake
)
set( CPM_SOURCE_CACHE "${CMAKE_CURRENT_BINARY_DIR}/deps" )
include( ${CMAKE_CURRENT_BINARY_DIR}/cmake/get_cpm.cmake )


# Add packages

set( boost_ver boost-1.86.0 )
CPMAddPackage( "gh:boostorg/static_assert#${boost_ver}"   ) # Boost::core dependency
CPMAddPackage( "gh:boostorg/throw_exception#${boost_ver}" ) # Boost::core dependency
CPMAddPackage( "gh:boostorg/config#${boost_ver}"          ) # Boost::core dependency
CPMAddPackage( "gh:boostorg/io#${boost_ver}"              ) # Boost::utility dependency
CPMAddPackage( "gh:boostorg/type_traits#${boost_ver}"     ) # Boost::utility dependency
CPMAddPackage( "gh:boostorg/predef#${boost_ver}"          ) # Boost::winapi dependency
CPMAddPackage( "gh:boostorg/assert#${boost_ver}"          )
CPMAddPackage( "gh:boostorg/core#${boost_ver}"            )
CPMAddPackage( "gh:boostorg/preprocessor#${boost_ver}"    )
CPMAddPackage( "gh:boostorg/stl_interfaces#${boost_ver}"  )
CPMAddPackage( "gh:boostorg/winapi#${boost_ver}"          )
CPMAddPackage( "gh:boostorg/utility#${boost_ver}"         )

CPMAddPackage( "gh:psiha/config_ex#master" )
CPMAddPackage( "gh:psiha/std_fix#master"   )
CPMAddPackage( "gh:psiha/err#master"       )
CPMAddPackage( "gh:psiha/build#master"     )

if ( ${CMAKE_SYSTEM_NAME} MATCHES "Linux" )
    set( PSI_USE_LINKER "lld" ) # for thinlto-cache-dir support
endif()
include( ${build_SOURCE_DIR}/build_options.cmake )

PSI_add_compile_options( Debug ${PSI_compiler_runtime_sanity_checks} )
PSI_add_link_options   ( Debug ${PSI_linker_runtime_sanity_checks}   )
  
PSI_add_compile_options( Release ${PSI_compiler_LTO} ${PSI_compiler_optimize_for_size} ${PSI_compiler_disable_thread_safe_init} ${PSI_compiler_fastmath} ${PSI_compiler_debug_symbols} )

if ( WIN32 )
  add_compile_definitions( WIN32_LEAN_AND_MEAN NOMINMAX NOCOMM )
endif()

if ( ${CMAKE_SYSTEM_NAME} MATCHES "Linux" )
    PSI_add_link_options( Release -flto ) # lld does not seem to be enough
    add_compile_options( -stdlib=libc++ )
    # Needed under WSL for some reason?
    PSI_add_link_options( Debug      -lc++ -lc++abi -lm -lubsan )
    PSI_add_link_options( DevRelease -lc++ -lc++abi -lm -lubsan )
    PSI_add_link_options( Release    -lc++ -lc++abi -lm         )
else()
    set( CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE true )
    PSI_add_link_options( Release ${PSI_linker_LTO} )
endif()


###################
## Target(s)
###################

include( vm.cmake )

target_link_libraries( psi_vm PUBLIC
  Boost::core
  Boost::assert
  Boost::preprocessor
  Boost::stl_interfaces
  Boost::winapi
  Boost::utility
)

target_include_directories( psi_vm PUBLIC
  "${build_SOURCE_DIR}/include"
  "${config_ex_SOURCE_DIR}/include"
  "${err_SOURCE_DIR}/include"
  "${std_fix_SOURCE_DIR}/include" # vm::vector uses it (in a header)
)

target_precompile_headers( psi_vm PUBLIC src/pch.hpp )


###################
## Testing
###################

enable_testing()
add_subdirectory( "${PROJECT_SOURCE_DIR}/test" )
