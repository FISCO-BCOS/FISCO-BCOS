# Official evmone (ipsilon) v0.21.0. FISCO-BCOS's SM3 (national crypto) support is
# applied as a transparent patch instead of consuming a private fork: evmone::VM
# carries an optional host-provided hash_fn used by the KECCAK256 opcode, so SM
# chains hash with SM3. The evmc/ headers are NOT modified (evmc_host_context stays
# opaque upstream). The patch also carries the macOS static-lib combine and the
# fork-parity exception-enabled build (noexcept stripped from the execute entry
# points; NOT an exception-propagation guarantee). See fisco-sm3.patch.
# Use the GitHub source archive (single tarball) rather than a full git history
# fetch: official evmone's history is large and vcpkg_from_git kept disconnecting
# mid-transfer. The archive contains the vendored evmc/ and lib/evmone_precompiles/
# (evmone's only submodule is the test-only evm-benchmarks, which we don't build).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ipsilon/evmone
    REF v0.21.0
    SHA512 bc2928d42140d2fbb47d1e06773e634d208945e52ac70a418798586897a60164910cc2b23c80479ae172941d8d9142ea6fdd86e13f560195cff44ccdc1f1d0f2
    HEAD_REF master
    PATCHES fisco-sm3.patch
)

vcpkg_cmake_configure(
    SOURCE_PATH ${SOURCE_PATH}
    OPTIONS
        -DBUILD_SHARED_LIBS=OFF
        -DEVMONE_TESTING=OFF
        -DEVMONE_FUZZING=OFF
        -DEVMONE_TOOLS=OFF
        -DHUNTER_ENABLED=OFF
)
vcpkg_cmake_install()

# Remove evmone's auto-generated cmake config (it lacks the imported target definition
# and previously referenced the non-existent evmc package). Our manual config below
# replaces it for both release and debug.
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/lib/cmake/evmone"
    "${CURRENT_PACKAGES_DIR}/debug/lib/cmake/evmone"
)

# 4. The vendored evmc headers are installed by the separate `evmc` port, not here. They are
#    needed by bcos-framework, which is built even when FULLNODE=OFF and therefore without
#    evmone; shipping them from this port made them unavailable to that configuration. Two ports
#    installing the same files is rejected by vcpkg, so this port must not install them as well.
#    evmone's own build is unaffected -- it compiles against the copy in its source tree.

# 4a. Install evmone precompiles static library because evmone.a depends on it transitively.
# Use platform-aware library name: .lib on Windows, .a on Unix.
if(VCPKG_TARGET_IS_WINDOWS)
    set(_precomp_lib_name "evmone_precompiles.lib")
else()
    set(_precomp_lib_name "libevmone_precompiles.a")
endif()

file(GLOB_RECURSE _precomp_dbg_libs
    "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/${_precomp_lib_name}")
file(GLOB_RECURSE _precomp_rel_libs
    "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/${_precomp_lib_name}")

if(NOT DEFINED VCPKG_BUILD_TYPE OR VCPKG_BUILD_TYPE STREQUAL "debug")
    if(NOT _precomp_dbg_libs)
        message(FATAL_ERROR "debug ${_precomp_lib_name} not found in evmone build tree")
    endif()
    list(GET _precomp_dbg_libs 0 _precomp_dbg_lib)
    file(INSTALL "${_precomp_dbg_lib}" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
endif()

if(NOT DEFINED VCPKG_BUILD_TYPE OR VCPKG_BUILD_TYPE STREQUAL "release")
    if(NOT _precomp_rel_libs)
        message(FATAL_ERROR "release ${_precomp_lib_name} not found in evmone build tree")
    endif()
    list(GET _precomp_rel_libs 0 _precomp_rel_lib)
    file(INSTALL "${_precomp_rel_lib}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
endif()

# 4c. Install phase-1 precompile crypto headers used directly by FISCO-BCOS.
#     (lib/evmone/*.hpp internal headers are installed by the patch's DIRECTORY install.)
file(INSTALL
    "${SOURCE_PATH}/lib/evmone_precompiles/sha256.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/ripemd160.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/kzg.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/blake2b.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/bn254.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/ecc.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/hash_types.h"
    "${SOURCE_PATH}/lib/evmone_precompiles/keccak.h"
    "${SOURCE_PATH}/lib/evmone_precompiles/keccak.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/secp256k1.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/mulmod.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/secp256r1.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/modexp.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/bls.hpp"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/evmone_precompiles")

file(INSTALL "${SOURCE_PATH}/lib/evmone_precompiles/pairing"
     DESTINATION "${CURRENT_PACKAGES_DIR}/include/evmone_precompiles")

# 4d. Install evmone's test/utils sources (headers AND .cpp) verbatim under their upstream
#     include prefix. bcos-evm consumes them directly (compiling the .cpp files from this
#     directory) instead of carrying an in-repo vendored copy; the upstream-quoted includes
#     ("statetest.hpp", "stdx/utility.hpp") resolve in-directory, and the <test/state/...>
#     cross-references are satisfied by the consumer (bcos-evm forwards them to its vendored
#     eth/state copy, which is byte-identical to upstream test/state modulo include paths).
#     Install only what bcos-evm actually consumes, not the whole harness. The closure is
#     rlp.hpp / rlp_encode.{hpp,cpp} / test_state.{hpp,cpp} plus stdx/ — verified by taking the
#     files bcos-evm includes and following their in-directory quoted includes to a fixed point.
#     A blanket glob additionally shipped statetest*, blockchaintest* and bytecode.hpp, two of
#     which (statetest.hpp, statetest_loader.cpp) include <nlohmann/json.hpp> — a dependency this
#     port does not declare, i.e. a header that cannot compile, placed on the include path of
#     every evmone consumer in the tree, not just this module.
file(INSTALL
        "${SOURCE_PATH}/test/utils/rlp.hpp"
        "${SOURCE_PATH}/test/utils/rlp_encode.hpp"
        "${SOURCE_PATH}/test/utils/rlp_encode.cpp"
        "${SOURCE_PATH}/test/utils/test_state.hpp"
        "${SOURCE_PATH}/test/utils/test_state.cpp"
     DESTINATION "${CURRENT_PACKAGES_DIR}/include/test/utils")
file(INSTALL "${SOURCE_PATH}/test/utils/stdx"
     DESTINATION "${CURRENT_PACKAGES_DIR}/include/test/utils")

# 4e. bcos-evm's state::transition() takes the node's chain id as an explicit trailing argument
#     (EIP-7702 step 1 must not compare tx.chain_id against itself — see
#     bcos-evm/eth/state/state.hpp). test_state.cpp is the only caller that cannot be edited in
#     tree, and leaving it on the upstream arity would force a default argument onto the
#     declaration, i.e. make the unsafe value the API's default for every future caller. Patch
#     this one call site to state tx.chain_id in its own source instead; it is a test helper, so
#     upstream behaviour is preserved exactly.
vcpkg_replace_string(
    "${CURRENT_PACKAGES_DIR}/include/test/utils/test_state.cpp"
    "get<state::TransactionProperties>(tx_props_or_error))"
    "get<state::TransactionProperties>(tx_props_or_error), tx.chain_id)")

# 5. Write a manual cmake config file (avoids install(EXPORT) issues)
#    Creates evmone::evmone imported target with proper dependencies.
#    Uses platform-aware library suffixes (.lib on Windows, .a on Unix).
set(EVMONE_CONFIG "${CURRENT_PACKAGES_DIR}/share/evmone/evmoneConfig.cmake")
file(WRITE "${EVMONE_CONFIG}" [=[
include(CMakeFindDependencyMacro)
find_dependency(intx)
find_dependency(blst)

if(NOT TARGET evmone::evmone)
    if(WIN32)
        set(_evmone_lib_prefix "")
        set(_evmone_lib_suffix ".lib")
    else()
        set(_evmone_lib_prefix "lib")
        set(_evmone_lib_suffix ".a")
    endif()

    add_library(evmone::evmone STATIC IMPORTED)
    set_target_properties(evmone::evmone PROPERTIES
        IMPORTED_LOCATION_RELEASE
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone${_evmone_lib_suffix}"
        IMPORTED_LOCATION_DEBUG
            "${CMAKE_CURRENT_LIST_DIR}/../../debug/lib/${_evmone_lib_prefix}evmone${_evmone_lib_suffix}"
        IMPORTED_LOCATION
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone${_evmone_lib_suffix}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_LINK_LIBRARIES
            "intx::intx;evmone::precompiles"
    )

    # Separate target for evmone_precompiles so blst can be a proper dependency
    add_library(evmone::precompiles STATIC IMPORTED)
    set_target_properties(evmone::precompiles PROPERTIES
        IMPORTED_LOCATION_RELEASE
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone_precompiles${_evmone_lib_suffix}"
        IMPORTED_LOCATION_DEBUG
            "${CMAKE_CURRENT_LIST_DIR}/../../debug/lib/${_evmone_lib_prefix}evmone_precompiles${_evmone_lib_suffix}"
        IMPORTED_LOCATION
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone_precompiles${_evmone_lib_suffix}"
        INTERFACE_LINK_LIBRARIES "blst"
    )

endif()
]=])

# Write version file
set(EVMONE_VERSION_CONFIG "${CURRENT_PACKAGES_DIR}/share/evmone/evmoneConfigVersion.cmake")
file(WRITE "${EVMONE_VERSION_CONFIG}" [=[
set(PACKAGE_VERSION "0.21.0")
if(PACKAGE_VERSION VERSION_LESS PACKAGE_FIND_VERSION)
    set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
    if(PACKAGE_FIND_VERSION STREQUAL PACKAGE_VERSION)
        set(PACKAGE_VERSION_EXACT TRUE)
    endif()
endif()
]=])

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
