include(ExternalProject)
include(GNUInstallDirs)

if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(TIKV_BUILD_MODE "debug")
else()
    set(TIKV_BUILD_MODE "release")
endif()

if (APPLE)
    set(CUSTOM_INSTALL_COMMAND "")
else()
    set(CUSTOM_INSTALL_COMMAND "${CMAKE_AR} d <SOURCE_DIR>/target/${TIKV_BUILD_MODE}/libtikv_client.a error.cc.o error_utils.cc.o memory_quota.cc.o parse_address.cc.o symbolize.cc.o tcp_client_posix.cc.o tcp_posix.cc.o unix_sockets_posix.cc.o uri_parser.cc.o")
endif()
find_program(CARGO_COMMAND NAMES cargo REQUIRED PATHS "${USER_HOME}\\.cargo\\bin")

ExternalProject_Add(tikv_client_project2
  PREFIX ${CMAKE_SOURCE_DIR}/deps
  GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/tikv-client-cpp.git
  GIT_TAG        63f78ebc451f6059a243481f4fe37b89bdb101a8
  BUILD_IN_SOURCE true
  # SOURCE_DIR     ${CMAKE_SOURCE_DIR}/deps/src/
  CONFIGURE_COMMAND ${CARGO_COMMAND} install cxxbridge-cmd@1.0.75
  BUILD_COMMAND ${CARGO_COMMAND} build && ${CARGO_COMMAND} build --release && make target/${TIKV_BUILD_MODE}/libtikv_client.a
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS <SOURCE_DIR>/target/${TIKV_BUILD_MODE}/libtikv_client.a
  # LOG_BUILD true
)

ExternalProject_Get_Property(tikv_client_project2 SOURCE_DIR)
ExternalProject_Get_Property(tikv_client_project2 BINARY_DIR)
set(KVCLIENT_INCLUDE_DIRS ${SOURCE_DIR}/include)
file(MAKE_DIRECTORY ${KVCLIENT_INCLUDE_DIRS})  # Must exist.

find_package(OpenSSL REQUIRED)

add_library(kv_client INTERFACE IMPORTED)
set_property(TARGET kv_client PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${KVCLIENT_INCLUDE_DIRS})
set_property(TARGET kv_client PROPERTY INTERFACE_LINK_LIBRARIES ${SOURCE_DIR}/target/${TIKV_BUILD_MODE}/libtikv_client.a OpenSSL::SSL OpenSSL::Crypto)

add_dependencies(kv_client tikv_client_project2)
