include(ExternalProject)
include(GNUInstallDirs)

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(SWSSL_CONFIG_COMMAND sh ./Configure darwin64-x86_64-cc)
else()
    set(SWSSL_CONFIG_COMMAND bash config -DOPENSSL_PIC no-shared)
endif ()

set(SWSSL_BUILD_COMMAND make)
ExternalProject_Add(swssl
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NAME swssl.tar.gz
        DOWNLOAD_NO_PROGRESS 1
        URL /home/maggie/5.25/swssl-20010-0018-3.10.0-1160.el7.x86_64.tgz
        URL_HASH SHA256=a773ff33207a54f7c2ac95a392d43131214cfe3140f2af58d2a86b42c0f47064
        # GIT_TAG ccdfc64c5f56988f76abc0390a12ed9865bc49e9
        # GIT_SHALLOW true
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND ""
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
)

ExternalProject_Get_Property(swssl SOURCE_DIR)
add_library(SWSSL STATIC IMPORTED)
set(SWSSL_SUFFIX .a)
set(SWSSL_INCLUDE_DIRS ${SOURCE_DIR}/include)
set(SWSSL_LIBRARY ${SOURCE_DIR}/lib/libssl${SWSSL_SUFFIX})
set(SWSSL_CRYPTO_LIBRARIE ${SOURCE_DIR}/lib/libcrypto${SWSSL_SUFFIX})
set(SWSSL_LIBRARIES ${SWSSL_LIBRARY} ${SWSSL_CRYPTO_LIBRARIE} dl)
set_property(TARGET SWSSL PROPERTY IMPORTED_LOCATION ${SWSSL_LIBRARIES})
set_property(TARGET SWSSL PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${SWSSL_INCLUDE_DIRS})

set(OPENSSL_INCLUDE_DIRS ${SWSSL_INCLUDE_DIRS})
set(OPENSSL_LIBRARIES ${SWSSL_LIBRARIES})
message(STATUS "libssl include  : ${SWSSL_INCLUDE_DIRS}")
message(STATUS "libssl libraries: ${SWSSL_LIBRARIES}")
