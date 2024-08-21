find_package(Boost REQUIRED program_options)
find_package(jsoncpp CONFIG REQUIRED)

add_library(${COMMAND_HELPER_LIB} CommandHelper.cpp)
target_link_libraries(${COMMAND_HELPER_LIB} PUBLIC bcos-framework Boost::program_options)

add_library(${PROTOCOL_INIT_LIB} ProtocolInitializer.cpp)
target_link_libraries(${PROTOCOL_INIT_LIB} PUBLIC
                     ${CRYPTO_TARGET} ${TOOL_TARGET} ${TARS_PROTOCOL_TARGET} ${SECURITY_TARGET})

add_library(${FRONTSERVICE_INIT_LIB} FrontServiceInitializer.cpp)
target_link_libraries(${FRONTSERVICE_INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} ${TOOL_TARGET} ${FRONT_TARGET})

add_library(${PBFT_INIT_LIB} PBFTInitializer.cpp ProPBFTInitializer.cpp)
target_link_libraries(${PBFT_INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} ${LEDGER_TARGET} ${TOOL_TARGET} ${SEALER_TARGET} ${RPBFT_TARGET} ${PBFT_TARGET} ${SYNC_TARGET} ${STORAGE_TARGET})
set_target_properties(${PBFT_INIT_LIB} PROPERTIES UNITY_BUILD "ON")
if(WITH_TIKV)
    target_link_libraries(${PBFT_INIT_LIB} PUBLIC ${LEADER_ELECTION_TARGET})
endif()

add_library(${TXPOOL_INIT_LIB} TxPoolInitializer.cpp)
target_link_libraries(${TXPOOL_INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} ${TOOL_TARGET} ${TXPOOL_TARGET})
include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

add_library(baseline_init BaselineSchedulerInitializer.cpp)
target_link_libraries(baseline_init transaction-scheduler transaction-executor ${STORAGE_TARGET})

add_library(${INIT_LIB} Initializer.cpp)
list(APPEND INIT_LIB_DEPENDS ${PROTOCOL_INIT_LIB} baseline_init ${FRONTSERVICE_INIT_LIB} ${TXPOOL_INIT_LIB} ${SCHEDULER_TARGET} ${STORAGE_TARGET} ${EXECUTOR_TARGET} ${RPC_TARGET} bcos-boostssl tomlplusplus::tomlplusplus)
if(WITH_TIKV)
    list(APPEND INIT_LIB_DEPENDS ${LEADER_ELECTION_TARGET})
endif()
if(WITH_LIGHTNODE)
    add_library(lightnodeinit LightNodeInitializer.cpp LightNodeInitializer.h)
    target_link_libraries(lightnodeinit PUBLIC bcos-lightnode bcos-concepts ${TARS_PROTOCOL_TARGET})
    list(APPEND INIT_LIB_DEPENDS lightnodeinit bcos-concepts)
endif()
target_link_libraries(${INIT_LIB} PUBLIC ${INIT_LIB_DEPENDS})
set_target_properties(${INIT_LIB} PROPERTIES UNITY_BUILD "ON")
add_dependencies(${INIT_LIB} BuildInfo.h)
