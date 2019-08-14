include(ExternalProject)
set(PAILLIER_LIB_SUFFIX a)

ExternalProject_Add(paillier
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NO_PROGRESS 1
    DOWNLOAD_NAME V1.0.tar.gz
    URL https://github.com/Shareong/paillier/archive/V1.0.tar.gz
    URL_HASH SHA256=a5271ec419e93813163fac4dd38e052d850119ada8ca4427f9ff2816eca5e03e
    BUILD_IN_SOURCE 1
    SOURCE_SUBDIR ./homadd/
    CMAKE_COMMAND ${CMAKE_COMMAND}
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    BUILD_COMMAND make
    INSTALL_COMMAND bash -c "cp *.${PAILLIER_LIB_SUFFIX} ${CMAKE_SOURCE_DIR}/deps/lib"
    BUILD_BYPRODUCTS ${CMAKE_SOURCE_DIR}/deps/lib/libpaillier.${PAILLIER_LIB_SUFFIX}
)

ExternalProject_Get_Property(paillier SOURCE_DIR)
add_library(Paillier STATIC IMPORTED)
set(PAILLIER_INCLUDE_DIR ${SOURCE_DIR}/homadd)
set(PAILLIER_LIBRARY ${CMAKE_SOURCE_DIR}/deps/lib/libpaillier.${PAILLIER_LIB_SUFFIX})
file(MAKE_DIRECTORY ${PAILLIER_INCLUDE_DIR})  # Must exist.
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/lib/)  # Must exist.

set_property(TARGET Paillier PROPERTY IMPORTED_LOCATION ${PAILLIER_LIBRARY})
set_property(TARGET Paillier PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${PAILLIER_INCLUDE_DIR})
add_dependencies(Paillier paillier)
unset(SOURCE_DIR)
