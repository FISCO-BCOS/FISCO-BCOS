include(ExternalProject)

ExternalProject_Add(zkg
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NO_PROGRESS 1
    DOWNLOAD_NAME libzkg-1.0.0.tar.gz
    URL https://github.com/FISCO-BCOS/libzkg/archive/v1.0.0.tar.gz
    URL_HASH SHA256=13125a062334a51381223f8f70e6a5e966eb597354c9cceef8cdcca52054807b
    BUILD_IN_SOURCE 1
    #CMAKE_COMMAND ""
    #CMAKE_ARGS ""
    #LOG_CONFIGURE 1
    #BUILD_COMMAND  "" 
    #LOG_BUILD 1
    #INSTALL_COMMAND 
    #COMMAND 
    #    cd ${CMAKE_SOURCE_DIR}/deps/src/zkg &&
    #    ar x ./circuit/libcircuit.a &&
    #    ar x ./circuit/libcircuit.a  &&
    #    ar x ./libsnark/libsnark/libsnark.a  &&
    #    ar x ./libsnark/libsnark/libsnark_adsnark.a  &&
    #    ar x ./libsnark/depends/libff/libff/libff.a  &&
    #    ar x ./libsnark/depends/libzm.a   &&
    #    ar x ./libsnark/depends/libsnark_supercop.a &&
    #    ar cru libzkg.a *.o &&
    #    ranlib libzkg.a 
    INSTALL_COMMAND ""
)

ExternalProject_Get_Property(zkg SOURCE_DIR)

#libzkg
add_library(Zkg STATIC IMPORTED)
set(LIBZKG_LIB "${SOURCE_DIR}/libzkg.a")
set(LIBZKG_INCLUDE_DIR "${SOURCE_DIR}/" )
set_property(TARGET Zkg PROPERTY IMPORTED_LOCATION ${LIBZKG_LIB} )
set_property(TARGET Zkg PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${LIBZKG_INCLUDE_DIR})
add_dependencies(Zkg zkg)

add_library(Zkg::circuit STATIC IMPORTED)
set_property(TARGET Zkg::circuit PROPERTY IMPORTED_LOCATION ${SOURCE_DIR}/circuit/libcircuit.a )
add_dependencies(Zkg::circuit zkg)

add_library(Zkg::snark STATIC IMPORTED)
set_property(TARGET Zkg::snark PROPERTY IMPORTED_LOCATION ${SOURCE_DIR}/libsnark/libsnark/libsnark.a )
add_dependencies(Zkg::snark zkg)

add_library(Zkg::ff STATIC IMPORTED)
set_property(TARGET Zkg::ff PROPERTY IMPORTED_LOCATION ${SOURCE_DIR}/libsnark/depends/libff/libff/libff.a )
add_dependencies(Zkg::ff zkg)

add_library(Zkg::zm STATIC IMPORTED)
set_property(TARGET Zkg::zm PROPERTY IMPORTED_LOCATION ${SOURCE_DIR}/libsnark/depends/libzm.a )
add_dependencies(Zkg::zm zkg)
unset(SOURCE_DIR)
