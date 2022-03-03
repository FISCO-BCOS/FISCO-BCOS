include(ExternalProject)
include(GNUInstallDirs)

#if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
#    if (ARCH_NATIVE)
#        set(TASSL_CONFIG_COMMAND bash config -DOPENSSL_PIC no-shared)
#    else()
#        set(TASSL_CONFIG_COMMAND sh ./Configure darwin64-x86_64-cc)
#    endif()
# else()
#    set(TASSL_CONFIG_COMMAND bash config -DOPENSSL_PIC no-shared)
# endif ()

set(TASSL_CONFIG_COMMAND bash config -DOPENSSL_PIC no-shared)

set(TASSL_BUILD_COMMAND make)
ExternalProject_Add(tassl
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NAME tassl_1.0.2o-5d2100b3.tar.gz
        DOWNLOAD_NO_PROGRESS 1
        URL https://github.com/FISCO-BCOS/TASSL/archive/5d2100b378063bc9ffce0bb703784ab6053848ce.tar.gz
        URL_HASH SHA1=34579b368b286f57efe041912d735be33e4f5975
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
