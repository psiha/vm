cmake_minimum_required( VERSION 3.27.0 )

project( vm )

# TODO: Fully implement

###################
##### Target ######
###################

file(GLOB_RECURSE vm_sources "${PROJECT_SOURCE_DIR}/include/*")
add_library( vm STATIC ${vm_sources} )
target_include_directories( vm PUBLIC "${PROJECT_SOURCE_DIR}/include/" )

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
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

# Add and link packages

CPMAddPackage("gh:psiha/build#master")
target_include_directories( vm PRIVATE "${build_SOURCE_DIR}/include" )

CPMAddPackage("gh:boostorg/boost#boost-1.84.0")
target_link_libraries( vm PRIVATE Boost::boost )

###################
##### Testing #####
###################

enable_testing()

# GOOGLE TEST
CPMAddPackage("gh:google/googletest@1.14.0")

# ADDING TESTS
add_subdirectory(${PROJECT_SOURCE_DIR}/test)