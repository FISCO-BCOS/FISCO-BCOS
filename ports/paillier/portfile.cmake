vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://github.com/FISCO-BCOS/paillier-lib.git"
    REF 8c9336a41e324f361bed60f1259e297db06b441a
)

# Patch: remove pkLen check in callpaillier.cpp
vcpkg_replace_string("${SOURCE_PATH}/paillierCpp/callpaillier.cpp"
    "if ((pkLen != 512 && pkLen != 1024) || cipher1.length() != cipherLen)"
    "if (cipher1.length() != cipherLen)"
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_BUILD_TYPE=Release
)

vcpkg_cmake_install()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

# Install custom cmake config
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/paillier-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/paillier")

# Install copyright
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
