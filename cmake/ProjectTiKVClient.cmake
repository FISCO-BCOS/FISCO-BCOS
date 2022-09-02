include(ExternalProject)
include(GNUInstallDirs)

ExternalProject_Add(tikv_client_project
  PREFIX ${CMAKE_SOURCE_DIR}/deps
  GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/tikv-client-c.git
  GIT_TAG        26380e0933c6588430e66495ad972492367d3675
  # SOURCE_DIR     ${CMAKE_SOURCE_DIR}/deps/src/
  PATCH_COMMAND  git submodule foreach --recursive git reset --hard #COMMAND export PATH=${PROTOC_EXECUTABLE_PATH}:\$PATH #COMMAND protoc --version
  CMAKE_ARGS -DHUNTER_ROOT=${CMAKE_SOURCE_DIR}/deps/hunter -DENABLE_TESTS=off -DBUILD_GMOCK=off -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DHUNTER_CONFIGURATION_TYPES=Release
  INSTALL_COMMAND bash <SOURCE_DIR>/create_link.sh ${CMAKE_SOURCE_DIR}/deps/Install
  BUILD_BYPRODUCTS <BINARY_DIR>/src/libkvclient.a
  # LOG_BUILD true
)

ExternalProject_Get_Property(tikv_client_project SOURCE_DIR)
ExternalProject_Get_Property(tikv_client_project BINARY_DIR)
set(KVCLIENT_INCLUDE_DIRS ${SOURCE_DIR}/include)
file(MAKE_DIRECTORY ${KVCLIENT_INCLUDE_DIRS})  # Must exist.

set(HUNTER_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/deps/Install")
file(MAKE_DIRECTORY ${HUNTER_INSTALL_PREFIX}/include)  # Must exist.

file(MAKE_DIRECTORY ${SOURCE_DIR}/third_party/libfiu/libfiu)  # Must exist.
file(MAKE_DIRECTORY ${SOURCE_DIR}/third_party/kvproto/cpp/)  # Must exist.

find_package(Boost REQUIRED context)
find_package(Protobuf CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
add_library(grpc_deps INTERFACE IMPORTED GLOBAL)
set_property(TARGET grpc_deps PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${HUNTER_INSTALL_PREFIX}/include)
set_property(TARGET grpc_deps PROPERTY INTERFACE_LINK_LIBRARIES ${HUNTER_INSTALL_PREFIX}/lib/libgrpc++.a ${HUNTER_INSTALL_PREFIX}/lib/libgrpc.a protobuf::libprotobuf
OpenSSL::SSL OpenSSL::Crypto Boost::context
${HUNTER_INSTALL_PREFIX}/lib/libcares.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libre2.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_hash.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_bad_variant_access.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_city.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_raw_hash_set.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_hashtablez_sampler.a
${HUNTER_INSTALL_PREFIX}/lib/libaddress_sorting.a
${HUNTER_INSTALL_PREFIX}/lib/libupb.a
${HUNTER_INSTALL_PREFIX}/lib/libgpr.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_status.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_cord.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_cordz_info.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_cordz_handle.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_cord_internal.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_cordz_functions.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_exponential_biased.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_bad_optional_access.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_synchronization.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_stacktrace.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_symbolize.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_debugging_internal.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_demangle_internal.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_graphcycles_internal.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_time.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_civil_time.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_time_zone.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_malloc_internal.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_str_format_internal.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_strings.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_strings_internal.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_int128.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_throw_delegate.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_base.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_raw_logging_internal.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_log_severity.a
${HUNTER_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libabsl_spinlock_wait.a
${HUNTER_INSTALL_PREFIX}/lib/libz.a
)

add_library(kv_client INTERFACE IMPORTED)
set_property(TARGET kv_client PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${HUNTER_INSTALL_PREFIX}/include ${KVCLIENT_INCLUDE_DIRS} ${SOURCE_DIR}/third_party/libfiu/libfiu ${SOURCE_DIR}/third_party/kvproto/cpp/)
set_property(TARGET kv_client PROPERTY INTERFACE_LINK_LIBRARIES ${BINARY_DIR}/src/libkv_client.a ${BINARY_DIR}/third_party/kvproto/cpp/libkvproto.a ${HUNTER_INSTALL_PREFIX}/lib/libPocoFoundation.a
grpc_deps)

add_dependencies(kv_client tikv_client_project)
