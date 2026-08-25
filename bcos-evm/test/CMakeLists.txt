find_package(Boost REQUIRED unit_test_framework)

add_executable(bcos-evm-eth-tests
    eth/EthTransitionTest.cpp
)
target_link_libraries(bcos-evm-eth-tests PRIVATE
    bcosevm::eth
    Boost::unit_test_framework
)
add_test(NAME BcosEvmEthTests COMMAND bcos-evm-eth-tests)

# opstack 库及其测试仅 FULLNODE 集成构建包含（依赖 bcos-framework/ledger/protocol-tars，
# standalone 下无这些目标/头）；BCOS_EVM_STANDALONE 分支只构建 bcos-evm-eth-tests。
if(NOT BCOS_EVM_STANDALONE)
    add_executable(bcos-evm-opstack-tests
        opstack/TestMain.cpp
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
        opstack/OpZeroFeeSpikeTest.cpp
        opstack/StateRootComputeTest.cpp
        opstack/OpCommonTest.cpp
        opstack/Storage2StateBridgeTest.cpp
        testutils/MemoryState.cpp
    )
    # Unity build over the 20 header-heavy test TUs; opstack/TestMain.cpp defines
    # BOOST_TEST_MODULE and stays out of the batch (project convention).
    set_target_properties(bcos-evm-opstack-tests PROPERTIES UNITY_BUILD "ON")
    set_source_files_properties(opstack/TestMain.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    target_include_directories(bcos-evm-opstack-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/opstack-executor)
    target_link_libraries(bcos-evm-opstack-tests PRIVATE
        bcosevm::opstack
        protocol-tars
        bcos-crypto
        bcos-utilities
        ledger
        Boost::unit_test_framework
    )
    add_test(NAME BcosEvmOpstackTests COMMAND bcos-evm-opstack-tests)
endif()
