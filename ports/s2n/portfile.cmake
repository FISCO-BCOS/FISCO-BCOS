# Overlay port: s2n's CMakeLists hard-codes CMAKE_FIND_PACKAGE_PREFER_CONFIG=TRUE,
# which routes find_package(OpenSSL) to the host's OpenSSL 3.x config and mixes it
# with the vcpkg Tassl/OpenSSL 1.1.1 build. prefer-module-mode-findopenssl.patch
# flips it back to module mode. Overlay ports bypass vcpkg version resolution, so
# s2n does NOT track builtin-baseline bumps while this port exists; remove it once
# the fix lands upstream. Tracking: https://github.com/FISCO-BCOS/FISCO-BCOS/issues/5663
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO aws/s2n-tls
    REF "v${VERSION}"
    SHA512 dc4340f14e5987d53bdac7a2b25411b599dd4024c4786a0be0f6e3bb271a8dfeef06d8ea945d39218f246dfa5adebf3915e8e585a821318c970adada09243714
    PATCHES
        fix-cmake-target-path.patch
        openssl.patch
        prefer-module-mode-findopenssl.patch
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        tests   BUILD_TESTING
)

set(EXTRA_ARGS)
if(VCPKG_TARGET_ARCHITECTURE STREQUAL "wasm32")
    set(EXTRA_ARGS "-DS2N_NO_PQ=TRUE")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${EXTRA_ARGS}
        ${FEATURE_OPTIONS}
        -DUNSAFE_TREAT_WARNINGS_AS_ERRORS=OFF
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/s2n/cmake)

if(BUILD_TESTING)
    message(STATUS "Testing")
    vcpkg_cmake_build(TARGET test LOGFILE_BASE test)
endif()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/lib/s2n"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/lib/s2n"
    "${CURRENT_PACKAGES_DIR}/share/s2n/modules"
)

# Handle copyright
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
