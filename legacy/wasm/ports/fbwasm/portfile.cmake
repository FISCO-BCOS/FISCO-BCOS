vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://github.com/FISCO-BCOS/bcos-wasm.git"
    REF 976b899355e1e485948d4d755304bfb8c8c09b87
)

# Find Rust toolchain
if(VCPKG_TARGET_IS_WINDOWS)
    set(USER_HOME "$ENV{USERPROFILE}")
else()
    set(USER_HOME "$ENV{HOME}")
endif()

find_program(CARGO_COMMAND NAMES cargo REQUIRED PATHS "${USER_HOME}/.cargo/bin")
find_program(RUSTUP_COMMAND NAMES rustup REQUIRED PATHS "${USER_HOME}/.cargo/bin")
find_program(RUSTC_COMMAND NAMES rustc REQUIRED PATHS "${USER_HOME}/.cargo/bin")

# Set rustc version
set(RUSTC_VERSION_REQUIRED "nightly-2024-02-25")
message(STATUS "Setting rustc to ${RUSTC_VERSION_REQUIRED}")
vcpkg_execute_build_process(
    COMMAND "${RUSTUP_COMMAND}" override set ${RUSTC_VERSION_REQUIRED} --path "${SOURCE_PATH}"
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME "rustup-override-${TARGET_TRIPLET}"
)

# Build with cargo
vcpkg_execute_build_process(
    COMMAND "${CARGO_COMMAND}" build
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME "cargo-build-debug-${TARGET_TRIPLET}"
)

vcpkg_execute_build_process(
    COMMAND "${CARGO_COMMAND}" build --release
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME "cargo-build-release-${TARGET_TRIPLET}"
)

# Determine build mode
if(VCPKG_BUILD_TYPE STREQUAL "debug")
    set(BCOS_WASM_BUILD_MODE "debug")
else()
    set(BCOS_WASM_BUILD_MODE "release")
endif()

# Install library and header
file(INSTALL "${SOURCE_PATH}/target/${BCOS_WASM_BUILD_MODE}/${CMAKE_STATIC_LIBRARY_PREFIX}bcos_wasm${CMAKE_STATIC_LIBRARY_SUFFIX}"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${SOURCE_PATH}/include/BCOS_WASM.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")

# Also install the other build mode for debug/release parity
if(VCPKG_BUILD_TYPE STREQUAL "debug")
    file(INSTALL "${SOURCE_PATH}/target/release/${CMAKE_STATIC_LIBRARY_PREFIX}bcos_wasm${CMAKE_STATIC_LIBRARY_SUFFIX}"
        DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
else()
    file(INSTALL "${SOURCE_PATH}/target/debug/${CMAKE_STATIC_LIBRARY_PREFIX}bcos_wasm${CMAKE_STATIC_LIBRARY_SUFFIX}"
        DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
    file(INSTALL "${SOURCE_PATH}/include/BCOS_WASM.h" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/include")
endif()

# Install custom cmake config
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/fbwasm-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/fbwasm")

# Install copyright
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
