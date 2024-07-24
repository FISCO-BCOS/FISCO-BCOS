include(ExternalProject)
include(GNUInstallDirs)

if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(TIKV_BUILD_MODE "debug")
else()
    set(TIKV_BUILD_MODE "release")
endif()

if (NOT CMAKE_AR)
    set(CMAKE_AR ar)
endif ()

if (APPLE)
    set(CUSTOM_INSTALL_COMMAND ${CMAKE_AR} -dv <SOURCE_DIR>/${TIKV_BUILD_MODE}/libtikvrust.a slice_buffer.cc.o)
else()
    set(CUSTOM_INSTALL_COMMAND ${CMAKE_AR} d <SOURCE_DIR>/${TIKV_BUILD_MODE}/libtikvrust.a error.cc.o error_utils.cc.o memory_quota.cc.o parse_address.cc.o symbolize.cc.o tcp_client_posix.cc.o tcp_posix.cc.o unix_sockets_posix.cc.o uri_parser.cc.o)
endif()
find_program(CARGO_COMMAND NAMES cargo REQUIRED PATHS "${USER_HOME}\\.cargo\\bin")
find_package(OpenSSL REQUIRED)
# make rust to use the same openssl as c++
set(ENV{OPENSSL_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR})
set(ENV{OPENSSL_LIB_DIR} ${OPENSSL_CRYPTO_LIBRARY})
set(ENV{OPENSSL_DIR} ${OPENSSL_INCLUDE_DIR}/../)
set(ENV{PROTOC} ${OPENSSL_INCLUDE_DIR}/../tools/protobuf/protoc)

ExternalProject_Add(tikv_client_cpp
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/tikv-client-cpp.git
        GIT_TAG 7a2a2ffd293c2890fb2fe3bc38c204e80833e985

        BUILD_IN_SOURCE true
        #   PATCH_COMMAND ${CARGO_COMMAND} install cxxbridge-cmd@1.0.75
        #   BUILD_COMMAND ${CARGO_COMMAND} build && ${CARGO_COMMAND} build --release && make target/${TIKV_BUILD_MODE}/libtikv_client.a
        #   BUILD_COMMAND cmake -S . -B . && cmake --build .
        CMAKE_ARGS -DPROTOC=${OPENSSL_INCLUDE_DIR}/../tools/protobuf/protoc -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DOPENSSL_DIR=${OPENSSL_INCLUDE_DIR}/../
        # INSTALL_COMMAND "${CUSTOM_INSTALL_COMMAND}"
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS <SOURCE_DIR>/libtikvcpp.a <SOURCE_DIR>/${TIKV_BUILD_MODE}/libtikvrust.a
        LOG_BUILD true
  LOG_INSTALL true
)

ExternalProject_Get_Property(tikv_client_cpp SOURCE_DIR)
ExternalProject_Get_Property(tikv_client_cpp BINARY_DIR)
set(KVCLIENT_INCLUDE_DIRS ${SOURCE_DIR}/include ${SOURCE_DIR}/cxxbridge/client-cpp/src)
file(MAKE_DIRECTORY ${KVCLIENT_INCLUDE_DIRS})  # Must exist.

add_library(kv_client INTERFACE IMPORTED)
set_property(TARGET kv_client PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${KVCLIENT_INCLUDE_DIRS})
set_property(TARGET kv_client PROPERTY INTERFACE_LINK_LIBRARIES ${SOURCE_DIR}/libtikvcpp.a ${SOURCE_DIR}/${TIKV_BUILD_MODE}/libtikvrust.a OpenSSL::SSL OpenSSL::Crypto)

add_dependencies(kv_client tikv_client_cpp)
