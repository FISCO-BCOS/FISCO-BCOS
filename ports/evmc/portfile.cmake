# EVMC headers only, split out of the evmone port.
#
# bcos-framework/ledger/LedgerConfig.h has used evmc types (evmc::bytes32, evmc_uint256be,
# evmc_revision) in its public interface since 2024-04, and bcos-framework is built
# unconditionally -- including the Windows CI job, which configures with -DFULLNODE=OFF. The
# headers used to arrive only as a side effect of installing evmone, a fullnode-feature
# dependency, so that job could not compile bcos-framework at all. (It never got far enough to
# notice: vcpkg install died earlier, on gmp.)
#
# The headers are vendored inside the evmone source archive, so this port fetches the same
# archive and installs nothing but evmc/include/evmc. evmone no longer installs them and depends
# on this port instead -- two ports must not install the same files, vcpkg rejects that outright.
#
# Keep REF/SHA512 in lockstep with ports/evmone/portfile.cmake. They name the same upstream
# archive, so vcpkg downloads it once and both ports extract from the cached copy.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ipsilon/evmone
    REF v0.24.0
    SHA512 83351c486c0638e8cae7fb826c9341ccadc1362d46346a331451695b53a79366e8354843bed431afddd0102221c48644d38385330f1198b5705bb2e8c60c6d53
    HEAD_REF master
)

file(INSTALL "${SOURCE_PATH}/evmc/include/evmc/" DESTINATION "${CURRENT_PACKAGES_DIR}/include/evmc")

# Header-only: no libraries, so no debug tree and nothing for vcpkg's post-build lib checks.
set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)
