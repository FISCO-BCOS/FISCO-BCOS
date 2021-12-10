include(ExternalProject)
include(GNUInstallDirs)
find_program(CARGO_COMMAND cargo)
find_program(RUSTUP_COMMAND rustup)
if(NOT CARGO_COMMAND OR NOT RUSTUP_COMMAND)
    message(FATAL_ERROR "cargo/rustup is not installed")
endif()
find_program(RUSTC_COMMAND rustc)
if(NOT CARGO_COMMAND OR NOT RUSTC_COMMAND)
    message(FATAL_ERROR "rustc is not installed")
endif()

execute_process(COMMAND ${RUSTC_COMMAND} -V OUTPUT_VARIABLE RUSTC_VERSION_INFO OUTPUT_STRIP_TRAILING_WHITESPACE)
STRING(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" RUSTC_VERSION ${RUSTC_VERSION_INFO})
# if (${RUSTC_VERSION} VERSION_LESS "1.53.0")
#     message(STATUS "rustc ${RUSTC_VERSION} is too old, executing `rustup update` to update it.")
#     execute_process(COMMAND rustup update)
# endif()

# same as https://github.com/WeBankBlockchain/WeDPR-Lab-Crypto/blob/main/rust-toolchain
if(NOT RUSTC_VERSION_REQUIRED)
    set(RUSTC_VERSION_REQUIRED "nightly-2021-06-17")
endif()
message(STATUS "set rustc to ${RUSTC_VERSION_REQUIRED} of path ${CMAKE_SOURCE_DIR}/deps/src/")
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/src/)
execute_process(COMMAND rustup override set ${RUSTC_VERSION_REQUIRED} --path ${CMAKE_SOURCE_DIR}/deps/src/ OUTPUT_QUIET ERROR_QUIET)

set(USE_WASMER OFF)
if(USE_WASMER)
    set(USE_WASMTIME OFF)
    set(WASM_ENGINE_LIBRARY "wasmer_c_api")
else()
    set(USE_WASMTIME ON)
    set(WASM_ENGINE_LIBRARY "wasmtime")
endif()

ExternalProject_Add(hera_project
        PREFIX ${CMAKE_SOURCE_DIR}/deps
        DOWNLOAD_NO_PROGRESS 1
        GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/hera.git
        GIT_SHALLOW false
        GIT_TAG ff28b0f817244db1f9d16f9eb409e5b1aeecf5d2
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                   -DBUILD_SHARED_LIBS=OFF
                   -DHERA_WASMTIME=${USE_WASMTIME}
                   -DHERA_WASMER=${USE_WASMER}
                   -DHERA_WASMER_NATIVE_ENGINE=OFF
                   -DHERA_WASMER_LLVM_BACKEND=OFF
                   -DHERA_DEBUGGING=${DEBUG}
                   -DEVMC_ROOT=${EVMC_ROOT}
                   -DHUNTER_STATUS_DEBUG=ON
                   -DHUNTER_USE_CACHE_SERVERS=NO
        BUILD_IN_SOURCE 1
        # BUILD_COMMAND cmake --build . -- -j
        LOG_DOWNLOAD 1
        LOG_CONFIGURE 1
        LOG_BUILD 1
        LOG_INSTALL 1
        BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libevmone.a <INSTALL_DIR>/lib/libhera-buildinfo.a <INSTALL_DIR>/lib/lib${WASM_ENGINE_LIBRARY}.a
)

ExternalProject_Get_Property(hera_project INSTALL_DIR)
set(HERA_INCLUDE_DIRS ${INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${HERA_INCLUDE_DIRS})  # Must exist.
if(DEBUG)
    set(HERA_LIBRARIES ${HERA_LIBRARIES} ${EVMC_INSTRUCTIONS_LIBRARIES})
endif()
if(NOT APPLE)
    set(HERA_LIBRARIES ${HERA_LIBRARIES} rt)
endif()
add_library(${WASM_ENGINE_LIBRARY} STATIC IMPORTED)
set_property(TARGET ${WASM_ENGINE_LIBRARY} PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/lib${WASM_ENGINE_LIBRARY}.a)

add_library(hera-buildinfo STATIC IMPORTED)
set_property(TARGET hera-buildinfo PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libhera-buildinfo.a)

add_library(hera STATIC IMPORTED)
set_property(TARGET hera PROPERTY IMPORTED_LOCATION ${INSTALL_DIR}/lib/libhera.a)
set_property(TARGET hera PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${HERA_INCLUDE_DIRS})
# add_dependencies(hera_project evmc)
add_dependencies(hera hera_project ${WASM_ENGINE_LIBRARY} hera-buildinfo)
