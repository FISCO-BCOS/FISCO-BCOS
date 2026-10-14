vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://github.com/supranational/blst.git"
    REF 54e6e55674722fc2797ebb4bbb71b26d881eb4b8
)

# Build using build.sh (blst's native build system)
set(BLST_CC "${CMAKE_C_COMPILER}")
# build.sh honours ${CFLAGS} on every compile, but NOT the CMake sysroot variables (they
# are empty in a port's scope, so a bare ${CC} compiles without the macOS SDK and
# blst>=0.3.17's C23 includes fail with 'stdlib.h' file not found). Pass the sysroot the
# way the runner resolves it instead.
if(CMAKE_HOST_APPLE)
    find_program(BLST_XCRUN_TOOL xcrun)
    if(BLST_XCRUN_TOOL)
        execute_process(COMMAND xcrun --show-sdk-path
            OUTPUT_VARIABLE BLST_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(BLST_SDK_PATH)
            set(BLST_CFLAGS "-isysroot ${BLST_SDK_PATH}")
        endif()
    endif()
endif()
if(CMAKE_C_OSX_DEPLOYMENT_TARGET_FLAG AND CMAKE_OSX_DEPLOYMENT_TARGET)
    set(BLST_CC "${BLST_CC} ${CMAKE_C_OSX_DEPLOYMENT_TARGET_FLAG}${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

find_program(BASH_COMMAND NAMES bash REQUIRED)

# CFLAGS is exported only when set: blst's build.sh uses ${CFLAGS:-default}, so an
# empty-but-set value would be fine, but a non-Apple host has no business passing the
# variable at all.
set(_blst_env "CC='${BLST_CC}' AR='${CMAKE_AR}' RANLIB='${CMAKE_RANLIB}' ")
if(BLST_CFLAGS)
    string(PREPEND _blst_env "CFLAGS='${BLST_CFLAGS}' ")
endif()

vcpkg_execute_build_process(
    COMMAND "${BASH_COMMAND}" "-lc" "${_blst_env}./build.sh"
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME "build-${TARGET_TRIPLET}"
)

# Install headers
file(INSTALL "${SOURCE_PATH}/bindings/blst.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/bindings/blst_aux.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")

# Install static library
set(BLST_LIB_PATH "${SOURCE_PATH}/${CMAKE_STATIC_LIBRARY_PREFIX}blst${CMAKE_STATIC_LIBRARY_SUFFIX}")
file(INSTALL "${BLST_LIB_PATH}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

# Install copyright
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)

# Install custom cmake config that creates plain 'blst' target
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/blst-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/blst")

# Create usage file
configure_file("${CMAKE_CURRENT_LIST_DIR}/usage" "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" @ONLY)
