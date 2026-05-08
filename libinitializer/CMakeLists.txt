find_package(Boost REQUIRED program_options)
find_package(jsoncpp CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

add_library(command CommandHelper.cpp)
target_link_libraries(command PUBLIC bcos-framework Boost::program_options)

add_library(protocol_init ProtocolInitializer.cpp)
target_link_libraries(protocol_init PUBLIC
                     bcos-crypto tool protocol-tars security)

add_library(front_init FrontServiceInitializer.cpp)
target_link_libraries(front_init PUBLIC protocol_init tool front)

add_library(pbft_init PBFTInitializer.cpp ProPBFTInitializer.cpp)
target_link_libraries(pbft_init PUBLIC protocol_init ledger tool sealer rpbft pbft sync storage)
set_target_properties(pbft_init PROPERTIES UNITY_BUILD "ON")
if(WITH_LEDGER_ELECTION)
    target_link_libraries(pbft_init PUBLIC leader_election)
endif()

add_library(txpool_init TxPoolInitializer.cpp)
target_link_libraries(txpool_init PUBLIC protocol_init tool txpool)

add_library(ledger_init LedgerInitializer.cpp)
target_link_libraries(ledger_init PRIVATE ledger)

add_library(baseline_init BaselineSchedulerInitializer.cpp MultiVersionScheduler.cpp)
target_link_libraries(baseline_init transaction-scheduler transaction-executor storage)

add_library(init Initializer.cpp)
list(APPEND INIT_LIB_DEPENDS protocol_init baseline_init front_init txpool_init scheduler ledger_init storage executor rpc bcos-boostssl tomlplusplus::tomlplusplus)
if(WITH_LEDGER_ELECTION)
    list(APPEND INIT_LIB_DEPENDS leader_election)
endif()
if(WITH_LIGHTNODE)
    add_library(lightnodeinit LightNodeInitializer.cpp LightNodeInitializer.h)
    target_link_libraries(lightnodeinit PUBLIC bcos-lightnode bcos-concepts protocol-tars)
    list(APPEND INIT_LIB_DEPENDS lightnodeinit bcos-concepts)
endif()
target_link_libraries(init PUBLIC ${INIT_LIB_DEPENDS})

set_target_properties(init PROPERTIES UNITY_BUILD "ON")
add_dependencies(init BuildInfo.h)
