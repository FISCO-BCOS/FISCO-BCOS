# EVMC headers only. Split out of the evmone overlay port so a build that needs the EVMC type
# declarations does not have to build an EVM.
#
# bcos-framework/ledger/LedgerConfig.h declares evmc_uint256be / evmc_revision members and is
# reached from bcos-tars-protocol/Common.h, which the C++ SDK builds. Depending on evmone to get
# those two declarations pulled in blst as well, and ports/blst builds through `bash -lc
# ./build.sh` with an empty CC under MSVC — the SDK-only Windows job died in configure before it
# ever reached a compiler. Nothing here is compiled: the install is 9 headers, no library, and
# their only includes are C/C++ standard headers.
#
# Same source archive, REF and SHA512 as ports/evmone, so the two cannot drift to different EVMC
# revisions. The fisco-sm3 patch is deliberately NOT applied: it touches evmone only, and the
# evmc/ headers are unmodified upstream (see the note at the top of ports/evmone/portfile.cmake).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ipsilon/evmone
    REF v0.21.0
    SHA512 bc2928d42140d2fbb47d1e06773e634d208945e52ac70a418798586897a60164910cc2b23c80479ae172941d8d9142ea6fdd86e13f560195cff44ccdc1f1d0f2
    HEAD_REF master
)

file(INSTALL "${SOURCE_PATH}/evmc/include/evmc/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/evmc")

# Header-only: no lib/, no debug/. vcpkg requires the policy to be stated explicitly.
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

file(INSTALL "${SOURCE_PATH}/LICENSE"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
