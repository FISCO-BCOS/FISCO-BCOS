find_package(Boost REQUIRED unit_test_framework)
find_package(evmone REQUIRED)
# rlp_encode.cpp left the production lib (zero callers); its only consumer, OpReceiptEncodeTest,
# ADL-calls evmone::state::rlp_encode. Re-derive EVMONE_TEST_UTILS_DIR here — set only in sibling bcos-evm scope.
get_target_property(_evmone_inc_dirs evmone::evmone INTERFACE_INCLUDE_DIRECTORIES)
list(GET _evmone_inc_dirs 0 _evmone_inc_dir)
cmake_path(NORMAL_PATH _evmone_inc_dir)
set(EVMONE_TEST_UTILS_DIR "${_evmone_inc_dir}/test/utils")

# opstack-executor-tests was the repo's only GTest target; converted to Boost.Test to drop the
# gtest dependency (not in vcpkg.json, broke CI configure). BOOST_TEST_MODULE is defined in the TU.
# Web3Transaction + tars aggregate init are not unity-build friendly here.
add_test(NAME OpstackExecutorTests COMMAND opstack-executor-tests)

find_package(jsoncpp CONFIG REQUIRED)

# 与既有 GTest target 同因：Web3Transaction + tars aggregate init 不宜 unity-build
# Vendored evmone test util — same -Wno-missing-field-initializers as the upstream build.
set_source_files_properties(${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
add_test(NAME OpstackExecutorBlockTests COMMAND opstack-executor-block-tests)

# Extracted pure-function layer (OpRlpDecode.h / Storage2StateHelpers.h): direct unit tests
# over the decode primitives and state-bridge helpers, no class-template instantiation.
add_test(NAME OpstackExecutorDetailTests COMMAND opstack-executor-detail-tests)
