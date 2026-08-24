vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://github.com/intel/ittapi.git"
    REF 2de8a23f6130036dcd4d1b78d05df3187951d298
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DITT_API_FORTRAN_SUPPORT=OFF
)

vcpkg_cmake_build()

# Manually install: ittapi doesn't have an install target
# Libraries are in the build directory's bin/ subdirectory
set(BUILD_DIR "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg")
if(NOT EXISTS "${BUILD_DIR}/bin/${CMAKE_STATIC_LIBRARY_PREFIX}ittnotify${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(BUILD_DIR "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel")
endif()

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${BUILD_DIR}/bin/${CMAKE_STATIC_LIBRARY_PREFIX}ittnotify${CMAKE_STATIC_LIBRARY_SUFFIX}"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

# Install headers
file(INSTALL "${SOURCE_PATH}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")

# Install copyright
file(INSTALL "${SOURCE_PATH}/LICENSES/BSD-3-Clause.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)

# Install custom cmake config
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/ittapi-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/ittapi")
