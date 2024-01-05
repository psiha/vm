cmake_minimum_required( VERSION 3.27.0 )

project( vm )

###################
##### Target ######
###################

set(CMAKE_CXX_STANDARD 23)

# TODO: Use psiha/build cmake utils?

file( GLOB_RECURSE vm_sources "${PROJECT_SOURCE_DIR}/include/*" )
add_library( vm STATIC ${vm_sources} )
target_include_directories( vm PUBLIC "${PROJECT_SOURCE_DIR}/include/" )
target_compile_features( vm PRIVATE cxx_std_23 )

###################
###### Deps #######
###################

# Download CPM.cmake

file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.3/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
  EXPECTED_HASH SHA256=cc155ce02e7945e7b8967ddfaff0b050e958a723ef7aad3766d368940cb15494
)
set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_BINARY_DIR}/deps")    # Using this ensures shallow clones
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)


# Add packages

CPMAddPackage("gh:boostorg/static_assert#boost-1.84.0")     # Boost::core dependency
CPMAddPackage("gh:boostorg/throw_exception#boost-1.84.0")   # Boost::core dependency
CPMAddPackage("gh:boostorg/config#boost-1.84.0")            # Boost::core dependency
CPMAddPackage("gh:boostorg/io#boost-1.84.0")                # Boost::utility dependency
CPMAddPackage("gh:boostorg/type_traits#boost-1.84.0")       # Boost::utility dependency
CPMAddPackage("gh:boostorg/predef#boost-1.84.0")            # Boost::winapi dependency
CPMAddPackage("gh:boostorg/assert#boost-1.84.0")
CPMAddPackage("gh:boostorg/core#boost-1.84.0")
CPMAddPackage("gh:boostorg/preprocessor#boost-1.84.0")
CPMAddPackage("gh:boostorg/winapi#boost-1.84.0")
CPMAddPackage("gh:boostorg/utility#boost-1.84.0")

CPMAddPackage("gh:psiha/config_ex#master")
CPMAddPackage("gh:psiha/std_fix#master")
CPMAddPackage("gh:psiha/err#master")
CPMAddPackage("gh:psiha/build#master")

CPMAddPackage("gh:google/googletest@1.14.0")

# Link & include

target_link_libraries( vm PRIVATE
        Boost::core
        Boost::assert
        Boost::preprocessor
        Boost::winapi
        Boost::utility
)

target_include_directories( vm PRIVATE
        "${assert_SOURCE_DIR}/include"
        "${core_SOURCE_DIR}/include"
        "${preprocessor_SOURCE_DIR}/include"
        "${winapi_SOURCE_DIR}/include"
        "${utility_SOURCE_DIR}/include"
        "${config_ex_SOURCE_DIR}/include"
        "${std_fix_SOURCE_DIR}/include"
        "${err_SOURCE_DIR}/include"
        "${build_SOURCE_DIR}/include"
)

###################
##### Testing #####
###################

enable_testing()

add_subdirectory("${PROJECT_SOURCE_DIR}/test")