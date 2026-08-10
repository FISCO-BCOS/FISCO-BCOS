# Minimal EEST fixtures download mechanism (Task 1 of opstack-phase1-eest-baseline).
#
# Scope note: the full eth-eest-test runner suite (src/ test/ runners/ include/,
# ~30 executable targets + 3 static libs + GTest) was intentionally NOT migrated.
# It was written against the worktree's bcos-evm API (bcos-evm/eth/{eip,apply,...},
# bcos-evm/opstack/OpStack*), which does not exist in the main tree (bcos-evm is
# evmone-based; only bcos-evm/eth/state is present). Pulling those targets into the
# main build would break `make all` on missing headers. Only the download mechanism
# is needed here: Task 2/3 drive the standalone `eest-runner` in
# ethereum-executor/tests, which reads EEST_FIXTURE_DIR at runtime.
#
# Exposes (CACHE INTERNAL): EVM_REF_EEST_ROOT (PARENT of fixtures/),
# EVM_REF_ETHEREUM_TESTS_ROOT. EEST_FIXTURE_DIR = ${EVM_REF_EEST_ROOT}/fixtures
# (directly contains state_tests/, blockchain_tests/, transaction_tests/).

option(BCOS_EVM_SPECS_TESTS_FETCH_ASSETS
    "Download pinned reference test assets via FetchContent at configure time"
    ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchReferenceAssets.cmake")
