# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/runner/work/vm/vm/build_clang/deps/static_assert/a4e1")
  file(MAKE_DIRECTORY "/home/runner/work/vm/vm/build_clang/deps/static_assert/a4e1")
endif()
file(MAKE_DIRECTORY
  "/home/runner/work/vm/vm/build_clang/_deps/static_assert-build"
  "/home/runner/work/vm/vm/build_clang/_deps/static_assert-subbuild/static_assert-populate-prefix"
  "/home/runner/work/vm/vm/build_clang/_deps/static_assert-subbuild/static_assert-populate-prefix/tmp"
  "/home/runner/work/vm/vm/build_clang/_deps/static_assert-subbuild/static_assert-populate-prefix/src/static_assert-populate-stamp"
  "/home/runner/work/vm/vm/build_clang/_deps/static_assert-subbuild/static_assert-populate-prefix/src"
  "/home/runner/work/vm/vm/build_clang/_deps/static_assert-subbuild/static_assert-populate-prefix/src/static_assert-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/runner/work/vm/vm/build_clang/_deps/static_assert-subbuild/static_assert-populate-prefix/src/static_assert-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/runner/work/vm/vm/build_clang/_deps/static_assert-subbuild/static_assert-populate-prefix/src/static_assert-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
