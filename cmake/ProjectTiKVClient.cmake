include(FetchContent)

if (APPLE)
    set(SED_CMMAND sed -i .bkp)
else()
    set(SED_CMMAND sed -i)
endif()

set(ENV{PATH} ${GRPC_ROOT}/bin:$ENV{PATH})
FetchContent_Declare(tikv_client_project
  GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/tikv-client-c.git
  GIT_TAG        e298ba30f711fef0c6ebd0d7a339a1a9e4133bd4
  # SOURCE_DIR     ${CMAKE_SOURCE_DIR}/deps/src/
  PATCH_COMMAND  git submodule foreach --recursive git reset --hard COMMAND export PATH=${GRPC_ROOT}/bin:\$PATH COMMAND protoc --version
  # LOG_BUILD true
)

if(NOT tikv_client_project_POPULATED)
  FetchContent_Populate(tikv_client_project)
  list(APPEND CMAKE_MODULE_PATH ${tikv_client_project_SOURCE_DIR}/cmake/)
  set(BUILD_SHARED_LIBS OFF)
  set(ENABLE_TESTS OFF)
  add_subdirectory(${tikv_client_project_SOURCE_DIR} ${tikv_client_project_BINARY_DIR})
  target_include_directories(kvproto PUBLIC ${GRPC_ROOT}/include)
  # target_compile_options(fiu PRIVATE -Wno-all  -Wno-error -Wno-unused-parameter)
  # target_compile_options(kvproto PRIVATE -Wno-all  -Wno-error -Wno-unused-parameter)
  # target_compile_options(kv_client PRIVATE -Wno-all -Wno-error -Wno-unused-parameter)
endif()

# add_library(bcos::tikv_client INTERFACE IMPORTED)
# set_property(TARGET bcos::tikv_client PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${tikv_client_project_SOURCE_DIR}/include ${GRPC_ROOT}/include)
# set_property(TARGET bcos::tikv_client PROPERTY INTERFACE_LINK_LIBRARIES kv_client gRPC::grpc++ Poco::Foundation)

install(
  TARGETS kv_client fiu kvproto
  EXPORT "${TARGETS_EXPORT_NAME}"
  # LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  # RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/bcos-storage"
)
