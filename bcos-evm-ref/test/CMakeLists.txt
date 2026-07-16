find_package(GTest CONFIG REQUIRED)

add_executable(bcos-evm-ref-eth-tests
    eth/EthTransitionTest.cpp
    eth/StateDiffWritebackTest.cpp
    eth/EestStateTest.cpp
    eth/EestBlockchainTest.cpp
)
target_link_libraries(bcos-evm-ref-eth-tests PRIVATE
    bcosevmref::eth
    GTest::gtest_main
)
add_test(NAME BcosEvmRefEthTests COMMAND bcos-evm-ref-eth-tests)

add_executable(bcos-evm-ref-opstack-tests
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
)
target_compile_definitions(bcos-evm-ref-opstack-tests PRIVATE
    EVM_REF_OPSTACK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/opstack"
)
target_link_libraries(bcos-evm-ref-opstack-tests PRIVATE
    bcosevmref::opstack
    GTest::gtest_main
)
add_test(NAME BcosEvmRefOpstackTests COMMAND bcos-evm-ref-opstack-tests)
