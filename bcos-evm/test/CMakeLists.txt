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
    # W6 Task 2: SeedPreStateTest uses jsoncpp (Json::Value/Json::Reader) for the
    # vector-pre -> StateDiff seeder. The target did not link jsoncpp before; without
    # this, Task 2's standalone build of bcos-evm-opstack-tests fails to link.
    find_package(jsoncpp CONFIG REQUIRED)
    target_sources(bcos-evm-opstack-tests PRIVATE
        opstack/support/SeedPreStateTest.cpp
        opstack/support/GoldenSampleTest.cpp
    )
    target_link_libraries(bcos-evm-opstack-tests PRIVATE jsoncpp_static)
    # W6 Task 4 L2 harness: real RPC parse path (parseNewPayloadRequest V4) over the 33 golden
    # vectors + the chained pair. Compiles EngineHelper.cpp directly (the parse layer; same
    # resolution as bcos-rpc's find_package) into this binary — the rest of bcos-rpc is not
    # linked. EngineHelper.cpp's include chain (bcos-framework/engine/Types.h +
    # bcos-rpc/jsonrpc/Common.h + utilities + jsoncpp) is covered by the include dirs/link
    # below.
    target_sources(bcos-evm-opstack-tests PRIVATE
        opstack/OpNewPayloadRpcE2eTest.cpp
        ${CMAKE_SOURCE_DIR}/bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineHelper.cpp
    )
    # EngineHelper.h 的 parseNewPayloadRequest 声明引用 bcos::protocol::TransactionFactory&，
    # 但 EngineHelper.h 不自声明（生产靠 bcos-rpc unity-build 的 include 顺序）。单 TU 直编时
    # 用 -include 把 TransactionFactory.h 前置（仅此源文件生效，不动生产代码）。
    set_source_files_properties(${CMAKE_SOURCE_DIR}/bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineHelper.cpp
        PROPERTIES COMPILE_FLAGS "-include bcos-framework/protocol/TransactionFactory.h")
    target_link_libraries(bcos-evm-opstack-tests PRIVATE
        codec protocol-tars ledger engine bcos-utilities jsoncpp_static)
    target_include_directories(bcos-evm-opstack-tests PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/bcos-ledger
        ${CMAKE_SOURCE_DIR}/bcos-rpc)
endif()
target_compile_definitions(bcos-evm-opstack-tests PRIVATE
    OP_T8N_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/opstack/t8n/vectors"
    OP_T8N_GOLDEN_ENGINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/opstack/t8n/golden/engine"
)
add_test(NAME BcosEvmOpstackTests COMMAND bcos-evm-opstack-tests)
