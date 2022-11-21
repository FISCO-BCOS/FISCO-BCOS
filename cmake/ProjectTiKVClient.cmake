include(ExternalProject)
include(GNUInstallDirs)

if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(TIKV_BUILD_MODE  "debug")
else()
    set(TIKV_BUILD_MODE "release")
endif()

find_program(CARGO_COMMAND NAMES cargo REQUIRED PATHS "${USER_HOME}\\.cargo\\bin")

# FIXME: when release 3.1.0 modify tikv-client log level to info
ExternalProject_Add(tikv_client_project
  PREFIX ${CMAKE_SOURCE_DIR}/deps
  GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/tikv-client-cpp.git
  GIT_TAG        20634f10560b9cf528ccff4c941ac0c4ef4c8ed2
  BUILD_IN_SOURCE true
  # SOURCE_DIR     ${CMAKE_SOURCE_DIR}/deps/src/
  CONFIGURE_COMMAND ${CARGO_COMMAND} install cxxbridge-cmd@1.0.75
  BUILD_COMMAND ${CARGO_COMMAND} build && ${CARGO_COMMAND} build --release && make target/${TIKV_BUILD_MODE}/libtikv_client.a
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS <SOURCE_DIR>/target/${TIKV_BUILD_MODE}/libtikv_client.a
  # LOG_BUILD true
)

ExternalProject_Get_Property(tikv_client_project SOURCE_DIR)
ExternalProject_Get_Property(tikv_client_project BINARY_DIR)
set(KVCLIENT_INCLUDE_DIRS ${SOURCE_DIR}/include)
file(MAKE_DIRECTORY ${KVCLIENT_INCLUDE_DIRS})  # Must exist.

find_package(OpenSSL REQUIRED)

add_library(kv_client INTERFACE IMPORTED)
set_property(TARGET kv_client PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${KVCLIENT_INCLUDE_DIRS})
set_property(TARGET kv_client PROPERTY INTERFACE_LINK_LIBRARIES ${SOURCE_DIR}/target/${TIKV_BUILD_MODE}/libtikv_client.a OpenSSL::SSL OpenSSL::Crypto)

add_dependencies(kv_client tikv_client_project)
