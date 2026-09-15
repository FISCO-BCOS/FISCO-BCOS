find_package(Boost REQUIRED program_options)
find_package(jsoncpp CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

# --- External-use targets: referenced outside libinitializer, must remain separate ---

# command: used by fisco-bcos-air, tars RpcService/GatewayService/ExecutorService/SchedulerService/NodeService
add_library(command CommandHelper.cpp)
target_link_libraries(command PUBLIC bcos-framework Boost::program_options)
add_dependencies(command BuildInfo.h)

# protocol_init: used by tars services and lightnode
add_library(protocol_init ProtocolInitializer.cpp Common.cpp)
target_link_libraries(protocol_init PUBLIC
                     bcos-crypto tool protocol-tars security)

# pbft_init: used by fisco-bcos-air, NodeService, storage-tool, archive-tool, hsm-tool
add_library(pbft_init PBFTInitializer.cpp ProPBFTInitializer.cpp)
target_link_libraries(pbft_init PUBLIC protocol_init ledger tool sealer rpbft pbft sync storage)
set_target_properties(pbft_init PROPERTIES UNITY_BUILD "ON")
add_dependencies(pbft_init BuildInfo.h)
if(WITH_LEDGER_ELECTION)
    target_link_libraries(pbft_init PUBLIC leader_election)
endif()

# --- init: merged from front_init, txpool_init, mempool_init, ledger_init, baseline_init, lightnodeinit ---
# These intermediate targets had no external consumers and are now folded directly into init.
add_library(init
    Initializer.cpp AuthInitializer.cpp
    FrontServiceInitializer.cpp
    TxPoolInitializer.cpp
    MemPoolInitializer.cpp
    LedgerInitializer.cpp
    GlobalStateStorageInitializer.cpp MultiVersionScheduler.cpp
    BaselineSchedulerInst.cpp
    OpEngineServiceInstantiate.cpp
)
target_link_libraries(init PUBLIC
    protocol_init
    tool front
    txpool
    mempool
    bcos-tx-validator
    single-consensus
    engine
    ledger
    transaction-scheduler transaction-executor
    ethereum-executor
    scheduler storage executor rpc
    bcos-boostssl
    tomlplusplus::tomlplusplus
)
# PRIVATE: only the implementation TUs (Initializer.cpp, OpEngineServiceInstantiate.cpp)
# include opstack headers — no installed init header does, so consumers of the init target
# need no opstack include dirs (the repo's usage-based PUBLIC/PRIVATE rule, cf. engine).
target_link_libraries(init PRIVATE
    bcosevm::opstack
    opstack-executor
)
if(WITH_LEDGER_ELECTION)
    target_link_libraries(init PUBLIC leader_election)
endif()
if(WITH_LIGHTNODE)
    target_sources(init PRIVATE LightNodeInitializer.cpp LightNodeInitializer.h)
    target_link_libraries(init PUBLIC bcos-lightnode bcos-concepts bcos-legacy)
endif()
if(WITH_TIKV)
    target_link_libraries(init PUBLIC storage-tikv bcos-legacy)
endif()
set_target_properties(init PROPERTIES UNITY_BUILD "ON")
add_dependencies(init BuildInfo.h)
if(TESTS)
    # enable_testing() BEFORE add_subdirectory, exactly as bcos-rpc/CMakeLists.txt does: ctest
    # registration is inherited through the parent scope, and this directory is added by the root
    # before the root's own enable_testing() runs. Without the call the child's
    # CTestTestfile.cmake is written but never linked from here, so an add_test below it is
    # compiled yet silently never executed — the suite runs nowhere.
    enable_testing()
    add_subdirectory(tests)
endif()
