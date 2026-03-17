# CMake generated Testfile for 
# Source directory: /home/runner/work/vm/vm/test
# Build directory: /home/runner/work/vm/vm/build_clang/test
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[vm_unit_tests]=] "vm_unit_tests")
set_tests_properties([=[vm_unit_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/vm/vm/test/CMakeLists.txt;40;add_test;/home/runner/work/vm/vm/test/CMakeLists.txt;0;")
subdirs("../_deps/googletest-build")
