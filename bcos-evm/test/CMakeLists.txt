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
    support/statetest_loader.cpp
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
    opstack/OpBlockHarnessTest.cpp
    opstack/OpZeroDiffTest.cpp
    opstack/OpReceiptMetaTest.cpp
    opstack/Op7702Test.cpp
    opstack/OpFloorGasTest.cpp
    opstack/OpBlockFinalizeTest.cpp
    opstack/OpBlockExecuteTest.cpp
    opstack/OpStateDiffSanitizeTest.cpp
    opstack/OpReceiptEncodeTest.cpp
    opstack/OpBlockSealTest.cpp
    opstack/OpT8nReplayTest.cpp
    opstack/MemoryLedgerT8nReplayTest.cpp
    opstack/MemoryLedgerTest.cpp
)
target_compile_definitions(bcos-evm-opstack-tests PRIVATE
    EVM_REF_OPSTACK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/opstack"
    OP_T8N_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/opstack/t8n/vectors"
)
# support/statetest_loader.cpp is vendored from evmone's test/utils/ tree, where it sits
# alongside statetest.hpp/utils.hpp/stdx/ and includes them unqualified ("statetest.hpp").
# It now lives outside eth/utils/ (test-only, not part of bcosevm::eth), so its quoted
# includes need the vendored eth/utils/ directory on the search path to resolve.
# TODO(eth-utils-removal): 向量装载链路整体替换——statetest.hpp/statetest_loader.cpp/
# utils/blob_schedule 换自研 JSON→账本装载器后,本 include 路径与 loader 源一并删除;
# 15 个用 TestState 的测试文件与 OpT8nReplayTest 同步迁自研账本(见移除清单文档)。
target_include_directories(bcos-evm-opstack-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bcos-evm/eth/utils
)
target_link_libraries(bcos-evm-opstack-tests PRIVATE
    bcosevm::opstack
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)
add_test(NAME BcosEvmOpstackTests COMMAND bcos-evm-opstack-tests)

# Storage2Ledger 桥测试仅在 in-tree(bcos-framework 目标存在,即根 CMakeLists.txt
# add_subdirectory(bcos-framework) 早于 add_subdirectory(bcos-evm))时编译;standalone
# 构建(bcos-evm/build,独立 vcpkg.json 不含 tbb/magic-enum/proxy/wedprcrypto 等
# bcos-framework 传递依赖)只交付 MemoryLedger 部分(plan Global Constraints,
# spec §7/§8 回填见 Task 7)。
if(TARGET bcos-framework)
    target_sources(bcos-evm-opstack-tests PRIVATE
        opstack/Storage2LedgerTest.cpp)
    target_link_libraries(bcos-evm-opstack-tests PRIVATE bcos-framework)
endif()
