include(ExternalProject)
include(GNUInstallDirs)

if(NOT DEFINED EXECUTOR_URL)
    set(EXECUTOR_URL https://${URL_BASE}/FISCO-BCOS/bcos-executor/archive/ac6d5d18bddfee86bcc41bedc5636bf7e11dc02e.tar.gz)
    set(EXECUTOR_URL_SHA256 2978dc81d730598c919663a569fd14a505879a510345c5d7949d8b72f3df1582)
endif()

ExternalProject_Add(bcos-executor
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NO_PROGRESS 1
        DOWNLOAD_NAME executor-${EXECUTOR_URL_SHA256}.tar.gz
	      URL ${EXECUTOR_URL}
        URL_HASH SHA256=${EXECUTOR_URL_SHA256}
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                   -DBUILD_SHARED_LIBS=off
        BUILD_IN_SOURCE 1
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
)
set(EXECUTOR_LIBRARY_SUFFIX ".a")
find_package(evmc CONFIG REQUIRED)

ExternalProject_Get_Property(bcos-executor INSTALL_DIR)
set(EXECUTOR_INCLUDE_DIRS ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${EXECUTOR_INCLUDE_DIRS})  # Must exist.

set(EXECUTOR_LIBRARY_PATH ${INSTALL_DIR}/${CMAKE_INSTALL_LIBDIR})
file(MAKE_DIRECTORY ${EXECUTOR_LIBRARY_PATH})  # Must exist.

add_library(bcos-executor::state STATIC IMPORTED)
set_target_properties(bcos-executor::state PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  INTERFACE_INCLUDE_DIRECTORIES ${EXECUTOR_INCLUDE_DIRS}
  IMPORTED_LOCATION ${EXECUTOR_LIBRARY_PATH}/libstate${EXECUTOR_LIBRARY_SUFFIX}
  INTERFACE_LINK_LIBRARIES "bcos-framework::table"
)
add_dependencies(bcos-executor::state bcos-executor)

set(EVMONE_LIB ${EXECUTOR_LIBRARY_PATH}/libevmone${EXECUTOR_LIBRARY_SUFFIX})
set(KECCAK_LIB ${EXECUTOR_LIBRARY_PATH}/libkeccak${EXECUTOR_LIBRARY_SUFFIX})
set(HERA_LIB ${EXECUTOR_LIBRARY_PATH}/libhera${EXECUTOR_LIBRARY_SUFFIX})
set(INTX_LIB ${EXECUTOR_LIBRARY_PATH}/libintx${EXECUTOR_LIBRARY_SUFFIX})
set(WASMTIME_LIB ${EXECUTOR_LIBRARY_PATH}/libwasmtime${EXECUTOR_LIBRARY_SUFFIX})
set(WABT_LIB ${EXECUTOR_LIBRARY_PATH}/libwabt${EXECUTOR_LIBRARY_SUFFIX})
set(HERA_BUILDINFO_LIB ${EXECUTOR_LIBRARY_PATH}/libhera-buildinfo${EXECUTOR_LIBRARY_SUFFIX})

add_library(bcos-executor::vm STATIC IMPORTED)
set_target_properties(bcos-executor::vm PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  INTERFACE_INCLUDE_DIRECTORIES ${EXECUTOR_INCLUDE_DIRS}
  IMPORTED_LOCATION ${EXECUTOR_LIBRARY_PATH}/libvm${EXECUTOR_LIBRARY_SUFFIX}
  INTERFACE_LINK_LIBRARIES "TBB::tbb;bcos-executor::state;bcos-framework::protocol;bcos-framework::utilities;Boost::program_options;wedpr-crypto::crypto;wedpr-crypto::extend-crypto;${EVMONE_LIB};${KECCAK_LIB};${INTX_LIB};${HERA_LIB};${HERA_BUILDINFO_LIB};${WASMTIME_LIB};evmc::loader;evmc::instructions;${WABT_LIB}"
)
add_dependencies(bcos-executor::vm bcos-executor)

add_library(bcos-executor::precompiled STATIC IMPORTED)
set_target_properties(bcos-executor::precompiled PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  INTERFACE_INCLUDE_DIRECTORIES ${EXECUTOR_INCLUDE_DIRS}
  IMPORTED_LOCATION ${EXECUTOR_LIBRARY_PATH}/libprecompiled${EXECUTOR_LIBRARY_SUFFIX}
  INTERFACE_LINK_LIBRARIES "bcos-framework::codec;bcos-framework::utilities;bcos-crypto::bcos-crypto;jsoncpp_lib_static;Boost::serialization;bcos-executor::vm"
)
add_dependencies(bcos-executor::precompiled bcos-executor)

add_library(bcos-executor::executor STATIC IMPORTED)
set_target_properties(bcos-executor::executor PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  INTERFACE_INCLUDE_DIRECTORIES ${EXECUTOR_INCLUDE_DIRS}
  IMPORTED_LOCATION ${EXECUTOR_LIBRARY_PATH}/libexecutor${EXECUTOR_LIBRARY_SUFFIX}
  INTERFACE_LINK_LIBRARIES "bcos-executor::vm;bcos-executor::precompiled;bcos-executor::state"
)
add_dependencies(bcos-executor::executor bcos-executor)

