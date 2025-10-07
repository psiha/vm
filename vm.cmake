# https://gitlab.kitware.com/cmake/cmake/-/issues/25725
# https://github.com/llvm/llvm-project/issues/83435
#...and yet and yet...still w/ Clang 19.5 VS 17.14.16
if ( CMAKE_CXX_COMPILER_ID MATCHES Clang AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT MATCHES MSVC )
    unset( CMAKE_CXX_STANDARD )
    add_compile_options( $<$<COMPILE_LANGUAGE:CXX>:/clang:-std=gnu++2c> )
else()
    set( CMAKE_CXX_STANDARD 26 )
    if ( MSVC ) # CMAKE_CXX_STANDARD is ignored??
        add_compile_options( -std:c++latest )
    endif()
endif()

# TODO finish and replace file globbing
#set( vm_public_headers
# "${CMAKE_CURRENT_LIST_DIR}/include/psi/vm/mapped_view/guarded_operation.hpp"
# "${CMAKE_CURRENT_LIST_DIR}/include/psi/vm/mapped_view/mapped_view.hpp"
# ...
#)
#set( vm_sources
# ...
# "${CMAKE_CURRENT_LIST_DIR}/src/protection.cpp"
#)
file( GLOB_RECURSE vm_public_headers "${CMAKE_CURRENT_LIST_DIR}/include/*.hpp" )
file( GLOB_RECURSE vm_sources        "${CMAKE_CURRENT_LIST_DIR}/src/*"         )

source_group( TREE ${CMAKE_CURRENT_LIST_DIR}/include/psi/vm FILES ${vm_public_headers} )
source_group( TREE ${CMAKE_CURRENT_LIST_DIR}/src            FILES ${vm_sources}        )

if ( WIN32 )
  set( excluded_impl posix )
else()
  set( excluded_impl win32 )
  set_source_files_properties( ${CMAKE_CURRENT_LIST_DIR}/src/detail/nt.cpp PROPERTIES HEADER_FILE_ONLY true )
endif()
foreach( source ${vm_sources} )
  if ( ${source} MATCHES ${excluded_impl} )
    set_source_files_properties( ${source} PROPERTIES HEADER_FILE_ONLY true )
  endif()
endforeach()

add_library( psi_vm STATIC ${vm_public_headers} ${vm_sources} )
add_library( psi::vm ALIAS psi_vm )
target_include_directories( psi_vm PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include" )
