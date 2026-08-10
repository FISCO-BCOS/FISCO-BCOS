# Data-only port: official ethereum/tests vectors, installed under share/ethereum-tests.
# Only the module-level vector directories are installed (~1 MB together) — the whole
# extracted repo is ~280 MB, dominated by the legacy consensus suites that EEST
# superseded, and copying those around per configure is pure waste. The two 20 MB
# "10Mb calldata" stress vectors under TransactionTests/ttData are dropped: they alone
# would grow the install forty-fold. To feed another module, add its directory to the
# list below.
#
# When bumping: pin a tag's commit SHA (tags stay reachable for the archive download),
# update REF and SHA512 together, and keep vcpkg.json's version in step with the tag.
# This port lives in ports-data/ (not ports/) on purpose: the CI vcpkg cache key hashes
# ports/**, and a data bump must not invalidate the compiled-dependency caches.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ethereum/tests
    REF c67e485ff8b5be9abc8ad15345ec21aa22e290d9 # tag v17.2
    SHA512 bd9d44e8d1709a8c5c006c320051457dc6b466565b1ef0c7286068d102875660f26ede0826175f467ec882c347e9b79d8a2c410b6e19acce4eb1bf676cb85e4a
    HEAD_REF develop
)

# Data-only: no headers or libraries by design.
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

foreach(dir TrieTests RLPTests TransactionTests ABITests GenesisTests)
    file(COPY "${SOURCE_PATH}/${dir}/"
        DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/${dir}")
endforeach()
file(REMOVE
    "${CURRENT_PACKAGES_DIR}/share/${PORT}/TransactionTests/ttData/String10MbData.json"
    "${CURRENT_PACKAGES_DIR}/share/${PORT}/TransactionTests/ttData/String10MbDataNotEnoughGAS.json")
file(INSTALL "${SOURCE_PATH}/LICENSE"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME copyright)
