# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/runner/work/vm/vm/_codeql_build_dir/deps/throw_exception/bd07")
  file(MAKE_DIRECTORY "/home/runner/work/vm/vm/_codeql_build_dir/deps/throw_exception/bd07")
endif()
file(MAKE_DIRECTORY
  "/home/runner/work/vm/vm/_codeql_build_dir/_deps/throw_exception-build"
  "/home/runner/work/vm/vm/_codeql_build_dir/_deps/throw_exception-subbuild/throw_exception-populate-prefix"
  "/home/runner/work/vm/vm/_codeql_build_dir/_deps/throw_exception-subbuild/throw_exception-populate-prefix/tmp"
  "/home/runner/work/vm/vm/_codeql_build_dir/_deps/throw_exception-subbuild/throw_exception-populate-prefix/src/throw_exception-populate-stamp"
  "/home/runner/work/vm/vm/_codeql_build_dir/_deps/throw_exception-subbuild/throw_exception-populate-prefix/src"
  "/home/runner/work/vm/vm/_codeql_build_dir/_deps/throw_exception-subbuild/throw_exception-populate-prefix/src/throw_exception-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/runner/work/vm/vm/_codeql_build_dir/_deps/throw_exception-subbuild/throw_exception-populate-prefix/src/throw_exception-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/runner/work/vm/vm/_codeql_build_dir/_deps/throw_exception-subbuild/throw_exception-populate-prefix/src/throw_exception-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
