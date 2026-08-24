vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://github.com/FISCO-BCOS/group-signature-lib.git"
    REF b9f3e2589b477c2cbe25472e3e30e49bf842a062
)

# Fix pbc_sig CMakeLists.txt: original tarball has backslash line continuations
# that produce invalid ninja build files.
# Copy fix script to cmake/ subdir, then run inject script
find_package(Python3 REQUIRED)
file(COPY "${CMAKE_CURRENT_LIST_DIR}/fix_pbc_sig_cmake.py" DESTINATION "${SOURCE_PATH}/cmake")
vcpkg_execute_build_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/inject_pbc_sig_fix.py"
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME "patch-${TARGET_TRIPLET}"
)

# ProjectPbc.cmake/ProjectPbcSig.cmake use PREFIX ${CMAKE_SOURCE_DIR}/deps (an
# absolute path in the shared source tree). CMake 3.30's ExternalProject writes
# deps/tmp/<name>-mkdirs.cmake before that dir exists, so pre-create it.
file(MAKE_DIRECTORY "${SOURCE_PATH}/deps/tmp")

# DISABLE_PARALLEL_CONFIGURE: debug and release configures share that same
# absolute deps/ PREFIX. Running them in parallel (vcpkg's default) races on the
# shared tree and intermittently fails in ExternalProject _ep_set_directories
# ("configure_file: No such file or directory"). Configure them sequentially.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    DISABLE_PARALLEL_CONFIGURE
    OPTIONS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

vcpkg_cmake_install()

# Manually install pbc and pbc_sig libraries (the project doesn't install them)
file(INSTALL "${SOURCE_PATH}/deps/lib/${CMAKE_STATIC_LIBRARY_PREFIX}pbc${CMAKE_STATIC_LIBRARY_SUFFIX}"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${SOURCE_PATH}/deps/src/pbc_sig/${CMAKE_STATIC_LIBRARY_PREFIX}pbc_sig${CMAKE_STATIC_LIBRARY_SUFFIX}"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

# Also install debug variants
file(INSTALL "${SOURCE_PATH}/deps/lib/${CMAKE_STATIC_LIBRARY_PREFIX}pbc${CMAKE_STATIC_LIBRARY_SUFFIX}"
    DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
file(INSTALL "${SOURCE_PATH}/deps/src/pbc_sig/${CMAKE_STATIC_LIBRARY_PREFIX}pbc_sig${CMAKE_STATIC_LIBRARY_SUFFIX}"
    DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

# Install custom cmake config
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/group-sig-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/group-sig")

# Install copyright
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
