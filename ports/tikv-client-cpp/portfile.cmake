vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://github.com/FISCO-BCOS/tikv-client-cpp.git"
    REF 7a2a2ffd293c2890fb2fe3bc38c204e80833e985
)

# Find Rust toolchain
if(VCPKG_TARGET_IS_WINDOWS)
    set(USER_HOME "$ENV{USERPROFILE}")
else()
    set(USER_HOME "$ENV{HOME}")
endif()
find_program(CARGO_COMMAND NAMES cargo REQUIRED PATHS "${USER_HOME}/.cargo/bin")

# Determine build mode
if(VCPKG_BUILD_TYPE STREQUAL "debug")
    set(TIKV_BUILD_MODE "debug")
else()
    set(TIKV_BUILD_MODE "release")
endif()

# Build using CMake - the vcpkg toolchain sets up OpenSSL/protobuf paths
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DCMAKE_BUILD_TYPE=${VCPKG_BUILD_TYPE}
)

vcpkg_cmake_install()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

# Install custom cmake config
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/tikv-client-cpp-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/tikv-client-cpp")

# Install copyright
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
