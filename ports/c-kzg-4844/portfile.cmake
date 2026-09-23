vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://github.com/ethereum/c-kzg-4844.git"
    REF 570d3fe7541912249a38e9b1a1954ce3108cc993 # v2.1.0
)

# Skip the debug build: the library is consumed through the release config only.
set(VCPKG_BUILD_TYPE release)

# Embed the mainnet trusted setup into the library as adjacent C string
# literals (one per line) so that no runtime file lookup is needed.
file(READ "${SOURCE_PATH}/src/trusted_setup.txt" TRUSTED_SETUP_TEXT)
string(REPLACE "\n" ";" TRUSTED_SETUP_LINES "${TRUSTED_SETUP_TEXT}")
set(TRUSTED_SETUP_LITERALS "")
foreach(LINE IN LISTS TRUSTED_SETUP_LINES)
    string(APPEND TRUSTED_SETUP_LITERALS "\"${LINE}\\n\"\n")
endforeach()
set(EMBED_C "${CURRENT_BUILDTREES_DIR}/trusted_setup_embed.c")
file(WRITE "${EMBED_C}"
    "const char ckzg_embedded_trusted_setup[] =\n${TRUSTED_SETUP_LITERALS};\n"
    "const unsigned long ckzg_embedded_trusted_setup_size = sizeof(ckzg_embedded_trusted_setup) - 1;\n"
)

# Upstream ships only a test-oriented Makefile; build with a small wrapper project.
set(WRAPPER_DIR "${CURRENT_BUILDTREES_DIR}/wrapper")
file(MAKE_DIRECTORY "${WRAPPER_DIR}")
file(WRITE "${WRAPPER_DIR}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.16)
project(ckzg C)

file(GLOB_RECURSE CKZG_SOURCES "${CKZG_ROOT}/src/*.c")
list(FILTER CKZG_SOURCES EXCLUDE REGEX "/test/")

find_path(BLST_INCLUDE_DIR NAMES blst.h REQUIRED)

add_library(ckzg STATIC ${CKZG_SOURCES} "${EMBED_C}" "${CKZG_PORT_DIR}/ckzg_embed.c")
target_include_directories(ckzg PRIVATE "${CKZG_ROOT}/src" "${CKZG_PORT_DIR}" "${BLST_INCLUDE_DIR}")
set_target_properties(ckzg PROPERTIES POSITION_INDEPENDENT_CODE ON)

install(TARGETS ckzg ARCHIVE DESTINATION lib)
install(FILES "${CKZG_ROOT}/src/ckzg.h" "${CKZG_PORT_DIR}/ckzg_embed.h" DESTINATION include)
install(
    DIRECTORY
        "${CKZG_ROOT}/src/common"
        "${CKZG_ROOT}/src/eip4844"
        "${CKZG_ROOT}/src/eip7594"
        "${CKZG_ROOT}/src/setup"
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
]=])

vcpkg_cmake_configure(
    SOURCE_PATH "${WRAPPER_DIR}"
    OPTIONS
        "-DCKZG_ROOT=${SOURCE_PATH}"
        "-DEMBED_C=${EMBED_C}"
        "-DCKZG_PORT_DIR=${CMAKE_CURRENT_LIST_DIR}"
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME c-kzg-4844)

# Replace the generated config with the plain-target config (mirrors the blst overlay).
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/c-kzg-4844")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/ckzg-config.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/c-kzg-4844"
    RENAME "c-kzg-4844-config.cmake"
)

file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/c-kzg-4844" RENAME copyright)
configure_file("${CMAKE_CURRENT_LIST_DIR}/usage" "${CURRENT_PACKAGES_DIR}/share/c-kzg-4844/usage" @ONLY)
