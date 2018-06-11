if( TARGET pbc )
    return()
endif()

unset( GMP_LIBRARIES CACHE )
unset( GMP_LIBRARIES )
unset( GMP_INCLUDE_DIR CACHE )
unset( GMP_INCLUDE_DIR )
unset( GMP_LIB_DIR CACHE )
unset( GMP_LIB_DIR )
unset( PBC_LIBRARIES CACHE )
unset( PBC_LIBRARIES )

unset( PBC_SIG_LIBRARIES CACHE )
unset( PBC_SIG_LIBRARIES )

unset( PBC_INCLUDE_DIR CACHE )
unset( PBC_INCLUDE_DIR )

unset( PBC_SIG_INCLUDE_DIR CACHE )
unset( PBC_SIG_INCLUDE_DIR )

# Locate GMP.
message( STATUS "Looking for GMP" )
if( WIN32 )
  set( GMP_INCLUDE_DIR "${GENERATEDGMP}")
  set( GMP_LIBRARIES "${GENERATEDGMP}/mpir.lib")
  find_library( GMP_LIBRARIES NAMES "mpir.lib" )
else()
   find_library( GMP_LIBRARIES NAMES "gmp" )
   find_path( GMP_INCLUDE_DIR "gmp.h" )
endif()
if( NOT GMP_LIBRARIES )
   message( FATAL_ERROR "GMP library not found" )
endif()
if( NOT GMP_INCLUDE_DIR )
   message( FATAL_ERROR "GMP includes not found" )
endif()
message(STATUS "GMP:${GMP_LIBRARIES}")


get_filename_component( GMP_LIB_DIR "${GMP_LIBRARIES}" DIRECTORY )

message( STATUS "Looking for PBC" )
if( WIN32 )
   find_library( PBC_LIBRARIES NAMES "pbclib.lib")
   find_library( PBC_SIG_LIBRARIES NAMES "pbc_siglib.lib")
   find_path( PBC_INCLUDE_DIR "pbc/include/pbc.h")
   find_path( PBC_SIG_NCLUDE_DIR "pbc/include/pbc_sig.h")
else()
   find_library( PBC_LIBRARIES NAMES "libpbc.a")
   find_library( PBC_SIG_LIBRARIES NAMES "libpbc_sig.a")
   find_path( PBC_INCLUDE_DIR "pbc/pbc.h")
   find_path( PBC_SIG_INCLUDE_DIR "pbc/pbc_sig.h")
endif()

add_library( pbc UNKNOWN IMPORTED )
set_property( TARGET pbc PROPERTY IMPORTED_LOCATION "${PBC_LIBRARIES}" )
set_property( TARGET pbc PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${PBC_INCLUDE_DIR}" "${GMP_INCLUDE_DIR}" )

add_library( pbc_sig UNKNOWN IMPORTED )
set_property( TARGET pbc_sig PROPERTY IMPORTED_LOCATION "${PBC_SIG_LIBRARIES}" )
set_property( TARGET pbc_sig PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${PBC_SIG_INCLUDE_DIR}" "${GMP_INCLUDE_DIR}" )

if( TARGET build_pbc )
    add_dependencies( pbc build_pbc )
endif()

mark_as_advanced(
    GMP_LIBRARIES
    GMP_INCLUDE_DIR
    GMP_LIB_DIR
    PBC_LIBRARIES
    PBC_SIG_LIBRARIES
    PBC_INCLUDE_DIR 
    PBC_SIG_INCLUDE_DIR )

