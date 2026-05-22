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
    BaselineSchedulerInitializer.cpp GlobalStateStorageInitializer.cpp MultiVersionScheduler.cpp
)
target_link_libraries(init PUBLIC
    protocol_init
    tool front
    txpool
    mempool
    ledger
    transaction-scheduler transaction-executor
    scheduler storage executor rpc
    bcos-boostssl
    tomlplusplus::tomlplusplus
)
if(WITH_LEDGER_ELECTION)
    target_link_libraries(init PUBLIC leader_election)
endif()
if(WITH_LIGHTNODE)
    target_sources(init PRIVATE LightNodeInitializer.cpp LightNodeInitializer.h)
    target_link_libraries(init PUBLIC bcos-lightnode bcos-concepts)
endif()
set_target_properties(init PROPERTIES UNITY_BUILD "ON")
add_dependencies(init BuildInfo.h)
