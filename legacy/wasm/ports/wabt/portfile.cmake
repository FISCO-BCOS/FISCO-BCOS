vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://github.com/WebAssembly/wabt.git"
    REF aa0515b3c808da880942db8658abeaa969534667
    FETCH_REF refs/tags/1.0.23
)

# Patch: wabt 1.0.23 needs CMAKE_PROJECT_VERSION set on Linux
if(NOT APPLE)
    vcpkg_replace_string("${SOURCE_PATH}/CMakeLists.txt"
        "project(wabt LANGUAGES C CXX VERSION \"1.0.23\")"
        "project(wabt LANGUAGES C CXX VERSION \"1.0.23\")\nset(CMAKE_PROJECT_VERSION \${PROJECT_VERSION})"
    )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTS=OFF
        -DBUILD_TOOLS=OFF
        -DBUILD_LIBWASM=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

vcpkg_cmake_install()

# Clean up debug directory
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")

# Install custom cmake config (wabt target name for compatibility)
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/wabt-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/wabt")

# Install copyright
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
