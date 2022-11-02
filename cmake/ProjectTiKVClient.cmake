include(ExternalProject)
include(GNUInstallDirs)

if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(TIKV_BUILD_MODE  "debug")
else()
    set(TIKV_BUILD_MODE "release")
endif()

# FIXME: when release 3.1.0 modify tikv-client log level to info
ExternalProject_Add(tikv_client_project
  PREFIX ${CMAKE_SOURCE_DIR}/deps
  GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/tikv-client-cpp.git
  GIT_TAG        b493417cfc74aaed966859370919d967535f413d
  BUILD_IN_SOURCE true
  # SOURCE_DIR     ${CMAKE_SOURCE_DIR}/deps/src/
  CONFIGURE_COMMAND cargo install cxxbridge-cmd@1.0.75
  BUILD_COMMAND cargo build && cargo build --release && make target/${TIKV_BUILD_MODE}/libtikv_client.a
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
