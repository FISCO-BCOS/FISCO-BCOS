include(ExternalProject)
include(GNUInstallDirs)

if(CMAKE_HOST_WIN32)
    set(USER_HOME "$ENV{USERPROFILE}")
else()
    set(USER_HOME "$ENV{HOME}")
endif()

message(${USER_HOME})

find_program(CARGO_COMMAND NAMES cargo REQUIRED PATHS "${USER_HOME}\\.cargo\\bin")
find_program(RUSTUP_COMMAND NAMES rustup REQUIRED PATHS "${USER_HOME}\\.cargo\\bin")

if(NOT CARGO_COMMAND OR NOT RUSTUP_COMMAND)
    message(FATAL_ERROR "cargo/rustup is not installed")
endif()

find_program(RUSTC_COMMAND NAMES rustc REQUIRED PATHS "${USER_HOME}\\.cargo\\bin")
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
    set(RUSTC_VERSION_REQUIRED "nightly-2024-02-25")
endif()
message(STATUS "set rustc to ${RUSTC_VERSION_REQUIRED} of path ${CMAKE_CURRENT_SOURCE_DIR}/deps/src/")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/src/)
execute_process(COMMAND rustup override set ${RUSTC_VERSION_REQUIRED} --path ${CMAKE_CURRENT_SOURCE_DIR}/deps/src/ OUTPUT_QUIET ERROR_QUIET)

if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(BCOS_WASM_BUILD_MODE "debug")
else()
    set(BCOS_WASM_BUILD_MODE "release")
endif()

if(NOT CMAKE_AR)
    set(CMAKE_AR "ar")
endif()

if (APPLE)
    set(CMAKE_AR_D ${CMAKE_AR} -d)
else()
    set(CMAKE_AR_D ${CMAKE_AR} d)
endif()

set(ZSTD_DUP_OBJECT "huf_compress.o")
if (NOT APPLE)
    list(APPEND ZSTD_DUP_OBJECT "zstd_common.o")
endif()

ExternalProject_Add(bcos_wasm_project
        PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/deps
        # DOWNLOAD_NO_PROGRESS 1
        GIT_REPOSITORY https://${URL_BASE}/FISCO-BCOS/bcos-wasm.git
        GIT_TAG 976b899355e1e485948d4d755304bfb8c8c09b87
        GIT_SHALLOW false
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ${CARGO_COMMAND} build && ${CARGO_COMMAND} build --release #&& ${CMAKE_AR_D} <SOURCE_DIR>/target/release/libbcos_wasm.a ${ZSTD_DUP_OBJECT} && ${CMAKE_AR_D} <SOURCE_DIR>/target/debug/libbcos_wasm.a ${ZSTD_DUP_OBJECT}
        INSTALL_COMMAND ""
        LOG_DOWNLOAD 1
        LOG_CONFIGURE 1
        LOG_BUILD 0
        LOG_INSTALL 1
        BUILD_BYPRODUCTS <SOURCE_DIR>/target/${BCOS_WASM_BUILD_MODE}/libbcos_wasm.a <SOURCE_DIR>/FBWASM.h
        )

ExternalProject_Get_Property(bcos_wasm_project SOURCE_DIR)
set(HERA_INCLUDE_DIRS ${SOURCE_DIR}/include)
file(MAKE_DIRECTORY ${HERA_INCLUDE_DIRS})  # Must exist.

add_library(fbwasm STATIC IMPORTED)
set_property(TARGET fbwasm PROPERTY IMPORTED_LOCATION ${SOURCE_DIR}/target/${BCOS_WASM_BUILD_MODE}/libbcos_wasm.a)
set_property(TARGET fbwasm PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${SOURCE_DIR}/include)
add_dependencies(fbwasm bcos_wasm_project evmc::instructions)
