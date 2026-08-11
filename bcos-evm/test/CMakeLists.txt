find_package(Boost REQUIRED unit_test_framework)

add_executable(bcos-evm-eth-tests
    eth/EthTransitionTest.cpp
)
target_link_libraries(bcos-evm-eth-tests PRIVATE
    bcosevm::eth
    Boost::unit_test_framework
)
add_test(NAME BcosEvmEthTests COMMAND bcos-evm-eth-tests)

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
    opstack/OpReceiptEncodeTest.cpp
    opstack/OpT8nReplayTest.cpp
    opstack/OpZeroFeeSpikeTest.cpp
)
target_link_libraries(bcos-evm-opstack-tests PRIVATE
    bcosevm::opstack
    Boost::unit_test_framework
)

# The two OP engine/scheduler smoke tests (OpSchedulerImplSmokeTest, OpEngineBranchSmokeTest)
# exercise the ported `bcos::evm::engine` headers (op-validator-loop engine OP branch) against
# the real `bcos::engine::EngineServiceImpl` + `OpSchedulerImpl` composition. They are
# in-tree-only, exactly like the source branch's engine tests: they need bcos-framework targets
# (codec / protocol-tars / ledger / engine) that exist only when the top-level CMakeLists.txt
# add_subdirectory(bcos-framework) runs first. A standalone bcos-evm build skips them.
if(TARGET bcos-framework)
    target_sources(bcos-evm-opstack-tests PRIVATE
        opstack/OpSchedulerImplSmokeTest.cpp
        opstack/OpEngineBranchSmokeTest.cpp
    )
    # OpSchedulerImpl.h uses bcos-codec/rlp decode primitives (RLPDecode.h, `codec`); makeOpHeader
    # constructs a bcostars::protocol::BlockHeaderImpl (`protocol-tars`); Storage2Ledger.h
    # (reached via OpSchedulerImpl.h) includes <bcos-ledger/mpt/Classify.h> and needs the `ledger`
    # library. The OP-engine-branch smoke test includes
    # <engine/bcos-engine/EngineServiceImpl.h> (needs the repo root on the include path) and needs
    # the `engine` library for the non-template detail:: helpers in EngineServiceImpl.cpp (the
    # source branch's "compiled in or linked" resolution; linking is used here to avoid compiling
    # the .cpp into this binary). `ledger`/`engine` are added by the root CMakeLists.txt AFTER
    # bcos-evm, so `if(TARGET ledger)` is false at this point even in a full in-tree build — the
    # target references resolve at generate time (same pattern as the source branch's test CMake).
    target_link_libraries(bcos-evm-opstack-tests PRIVATE
        codec protocol-tars ledger engine)
    target_include_directories(bcos-evm-opstack-tests PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/bcos-ledger)
    # W6 Task 2: SeedPreStateTest/GoldenSampleTest used jsoncpp (Json::Value) for the
    # vector-pre -> StateDiff seeder; both moved to opstack-executor in the split. The
    # find_package stays until the RPC harness tests (OpNewPayloadRpcE2eTest/OpL1EdgeGateTest,
    # linked below) move in Task 4 — they pull EngineHelper's jsoncpp include chain.
    find_package(jsoncpp CONFIG REQUIRED)
    # W6 Task 4 L2 harness: real RPC parse path (parseNewPayloadRequest V4) over the 33 golden
    # vectors + the chained pair. EngineHelper's parse/combine/serialize symbols are resolved from
    # `librpc.a` — the rpc library is already pulled into this binary transitively via `engine`
    # (Task 1 added `engine PRIVATE $<BUILD_INTERFACE:rpc>` for opEnvelopeToTars' Web3Transaction),
    # so compiling EngineHelper.cpp directly here would duplicate those symbols (7 link errors).
    # EngineHelper.h's include chain (bcos-framework/engine/Types.h + bcos-rpc/jsonrpc/Common.h +
    # utilities + jsoncpp) is covered by the include dirs/link below.
    target_sources(bcos-evm-opstack-tests PRIVATE
        opstack/OpNewPayloadRpcE2eTest.cpp
        opstack/OpL1EdgeGateTest.cpp
        opstack/OpEnvelopeToTarsTest.cpp
    )
    target_link_libraries(bcos-evm-opstack-tests PRIVATE
        codec protocol-tars ledger engine bcos-utilities jsoncpp_static rpc)
    target_include_directories(bcos-evm-opstack-tests PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/bcos-ledger
        ${CMAKE_SOURCE_DIR}/bcos-rpc)
endif()
add_test(NAME BcosEvmOpstackTests COMMAND bcos-evm-opstack-tests)
