find_package(Boost REQUIRED unit_test_framework)

add_executable(bcos-evm-eth-tests
    eth/EthTransitionTest.cpp
)
target_link_libraries(bcos-evm-eth-tests PRIVATE
    bcosevm::eth
    Boost::unit_test_framework
)
add_test(NAME BcosEvmEthTests COMMAND bcos-evm-eth-tests)

find_package(GTest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(bcos-evm-opstack-tests
    ${EVMONE_TEST_UTILS_DIR}/statetest_loader.cpp
    opstack/OpForkScheduleTest.cpp
    opstack/OpHostTest.cpp
    opstack/OpPrecompilesTest.cpp
    opstack/OpFeeParamsTest.cpp
    opstack/OpDepositTxTest.cpp
    opstack/OpPredeploysTest.cpp
    opstack/RollupCostTest.cpp
    opstack/OpValidateTest.cpp
    opstack/OpTransitionTest.cpp
    opstack/OpDepositTest.cpp
    opstack/OpZeroDiffTest.cpp
    opstack/OpReceiptMetaTest.cpp
    opstack/Op7702Test.cpp
    opstack/OpFloorGasTest.cpp
    opstack/OpReceiptEncodeTest.cpp
)
# statetest_loader.cpp is compiled straight from the evmone package's installed
# test/utils sources (see the EVMONE_TEST_UTILS_DIR probe in the top-level CMakeLists);
# its quoted includes ("statetest.hpp", "stdx/utility.hpp") resolve in-directory there.
# TODO(eth-utils-removal): 向量装载链路整体替换——statetest.hpp/statetest_loader.cpp/
# utils/blob_schedule 换自研 JSON→账本装载器后,本段包源编译一并删除;
# 15 个用 TestState 的测试文件与 OpT8nReplayTest 同步迁自研账本(见移除清单文档)。
target_link_libraries(bcos-evm-opstack-tests PRIVATE
    bcosevm::opstack
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)
add_test(NAME BcosEvmOpstackTests COMMAND bcos-evm-opstack-tests)
