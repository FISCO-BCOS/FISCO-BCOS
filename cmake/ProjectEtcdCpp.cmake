include(ExternalProject)
include(GNUInstallDirs)

ExternalProject_Add(etcd_cpp_project
  PREFIX ${CMAKE_SOURCE_DIR}/deps
  DOWNLOAD_NAME etcd-cpp-apiv3-4c6b281b.tar.gz
  DOWNLOAD_NO_PROGRESS 1
  GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/etcd-cpp-apiv3.git
  GIT_TAG        9d5eb060662409ce09e1642bfc429cc837667426
  # URL https://${URL_BASE}/FISCO-BCOS/etcd-cpp-apiv3/archive/704a0ea5ea4aeddc28f7d921135b6c34d00f06f1.tar.gz
  # URL_HASH SHA256=f935b8f3a7ca0fef1ae1214767000efd936a4102f662415240e3d95ee632def4
  # SOURCE_DIR     ${CMAKE_SOURCE_DIR}/deps/src/
  CMAKE_ARGS -DHUNTER_ROOT=${CMAKE_SOURCE_DIR}/deps/hunter -DBUILD_ETCD_TESTS=OFF
             -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DHUNTER_CONFIGURATION_TYPES=Release
             -DCMAKE_INSTALL_PREFIX=${CMAKE_SOURCE_DIR}/deps
  LOG_INSTALL true
  BUILD_BYPRODUCTS ${CMAKE_SOURCE_DIR}/deps/lib/libetcd-cpp-api.a
  # LOG_BUILD true
)

find_package(cpprestsdk CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
ExternalProject_Get_Property(etcd_cpp_project SOURCE_DIR)
ExternalProject_Get_Property(etcd_cpp_project BINARY_DIR)
set(ETCD_CPP_API_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/deps/include)
file(MAKE_DIRECTORY ${ETCD_CPP_API_INCLUDE_DIRS})  # Must exist.

add_library(etcd-cpp-api INTERFACE IMPORTED)
set_property(TARGET etcd-cpp-api PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${ETCD_CPP_API_INCLUDE_DIRS})
set_property(TARGET etcd-cpp-api PROPERTY INTERFACE_LINK_LIBRARIES ${CMAKE_SOURCE_DIR}/deps/${CMAKE_INSTALL_LIBDIR}/libetcd-cpp-api.a cpprestsdk::cpprest
)

add_dependencies(etcd-cpp-api etcd_cpp_project tikv_client_project)
