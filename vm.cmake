set( CMAKE_CXX_STANDARD 23 )

# TODO finish and replace file globbing
#set( vm_public_headers
# "${CMAKE_CURRENT_SOURCE_DIR}/include/psi/vm/mapped_view/guarded_operation.hpp"
# "${CMAKE_CURRENT_SOURCE_DIR}/include/psi/vm/mapped_view/mapped_view.hpp"
# ...
#)
#set( vm_sources
# ...
# "${CMAKE_CURRENT_SOURCE_DIR}/src/protection.cpp"
#)
file( GLOB_RECURSE vm_public_headers "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp" )
file( GLOB_RECURSE vm_sources        "${CMAKE_CURRENT_SOURCE_DIR}/src/*"         )

source_group( TREE ${CMAKE_CURRENT_SOURCE_DIR}/include/psi/vm FILES ${vm_public_headers} )
source_group( TREE ${CMAKE_CURRENT_SOURCE_DIR}/src            FILES ${vm_sources}        )

if ( WIN32 )
  set( excluded_impl posix )
else()
  set( excluded_impl win32 )
endif()
foreach( source ${vm_sources} )
  if ( ${source} MATCHES ${excluded_impl} )
    set_source_files_properties( ${source} PROPERTIES HEADER_FILE_ONLY true )
  endif()
endforeach()

add_library( psi_vm STATIC ${vm_public_headers} ${vm_sources} )
add_library( psi::vm ALIAS psi_vm )
target_include_directories( psi_vm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
target_compile_features( psi_vm PUBLIC cxx_std_23 )
