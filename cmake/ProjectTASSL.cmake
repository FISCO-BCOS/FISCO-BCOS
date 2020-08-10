include(ExternalProject)
include(GNUInstallDirs)

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(TASSL_CONFIG_COMMAND sh ./Configure darwin64-x86_64-cc)
else()
    set(TASSL_CONFIG_COMMAND bash config -DOPENSSL_PIC no-shared)
endif ()

set(TASSL_BUILD_COMMAND make)
ExternalProject_Add(tassl
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NAME tassl_1.0.2o-ccdfc64c.tar.gz
        DOWNLOAD_NO_PROGRESS 1
        URL https://github.com/jntass/TASSL/archive/ccdfc64c5f56988f76abc0390a12ed9865bc49e9.tar.gz
        URL_HASH SHA256=534c3ba12a75e6eb87aef4b86967c55a8845edd9e22be95ded27d1abd853f160
        # GIT_REPOSITORY https://github.com/jntass/TASSL.git
        # GIT_TAG ccdfc64c5f56988f76abc0390a12ed9865bc49e9
        # GIT_SHALLOW true
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND ${TASSL_CONFIG_COMMAND}
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
        BUILD_COMMAND ${TASSL_BUILD_COMMAND}
        INSTALL_COMMAND ""
)

ExternalProject_Get_Property(tassl SOURCE_DIR)
add_library(TASSL STATIC IMPORTED)
set(TASSL_SUFFIX .a)
set(TASSL_INCLUDE_DIRS ${SOURCE_DIR}/include)
set(TASSL_LIBRARY ${SOURCE_DIR}/libssl${TASSL_SUFFIX})
set(TASSL_CRYPTO_LIBRARIE ${SOURCE_DIR}/libcrypto${TASSL_SUFFIX})
set(TASSL_LIBRARIES ${TASSL_LIBRARY} ${TASSL_CRYPTO_LIBRARIE} dl)
set_property(TARGET TASSL PROPERTY IMPORTED_LOCATION ${TASSL_LIBRARIES})
set_property(TARGET TASSL PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${TASSL_INCLUDE_DIRS})

set(OPENSSL_INCLUDE_DIRS ${TASSL_INCLUDE_DIRS})
set(OPENSSL_LIBRARIES ${TASSL_LIBRARIES})
message(STATUS "libssl include  : ${TASSL_INCLUDE_DIRS}")
message(STATUS "libssl libraries: ${TASSL_LIBRARIES}")
