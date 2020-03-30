include(ExternalProject)
include(GNUInstallDirs)

ExternalProject_Add(snappy
    PREFIX ${CMAKE_SOURCE_DIR}/deps
    DOWNLOAD_NAME snappy-1.1.7.tar.gz
    DOWNLOAD_NO_PROGRESS 1
    URL https://codeload.github.com/google/snappy/tar.gz/1.1.7
        https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/libs/snappy-1.1.7.tar.gz
    URL_HASH SHA256=3dfa02e873ff51a11ee02b9ca391807f0c8ea0529a4924afa645fbf97163f9d4
    CMAKE_COMMAND ${CMAKE_COMMAND}
    CMAKE_ARGS  -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_POSITION_INDEPENDENT_CODE=${BUILD_SHARED_LIBS}
    ${_only_release_configuration}
    -DSNAPPY_BUILD_TESTS=Off
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    BUILD_IN_SOURCE 1
    LOG_CONFIGURE 1
    LOG_BUILD 1
    LOG_INSTALL 1
    # BUILD_COMMAND make
    ${_overwrite_install_command}
    BUILD_BYPRODUCTS <INSTALL_DIR>/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}snappy${CMAKE_STATIC_LIBRARY_SUFFIX}
)

ExternalProject_Get_Property(snappy INSTALL_DIR)
add_library(Snappy STATIC IMPORTED)
set(SNAPPY_INCLUDE_DIR ${INSTALL_DIR}/include/)
set(SNAPPY_LIBRARY ${INSTALL_DIR}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}snappy${CMAKE_STATIC_LIBRARY_SUFFIX})

set_property(TARGET Snappy PROPERTY IMPORTED_LOCATION ${SNAPPY_LIBRARY})
set_property(TARGET Snappy PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${SNAPPY_INCLUDE_DIR})
add_dependencies(Snappy snappy)

unset(INSTALL_DIR)
