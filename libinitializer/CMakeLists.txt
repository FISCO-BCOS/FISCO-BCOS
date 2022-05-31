file(GLOB SRC_LIST "*.cpp")

find_package(Boost REQUIRED program_options)
find_package(jsoncpp CONFIG REQUIRED)

add_library(${COMMAND_HELPER_LIB} CommandHelper.cpp)
target_link_libraries(${COMMAND_HELPER_LIB} bcos-framework Boost::program_options)

add_library(${PROTOCOL_INIT_LIB} ProtocolInitializer.cpp)
target_compile_options(${PROTOCOL_INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${PROTOCOL_INIT_LIB}
                     ${CRYPTO_TARGET} ${TOOL_TARGET} ${TARS_PROTOCOL_TARGET} ${SECURITY_TARGET})

add_library(${FRONTSERVICE_INIT_LIB} FrontServiceInitializer.cpp)
# target_compile_options(${FRONTSERVICE_INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${FRONTSERVICE_INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} ${TOOL_TARGET} ${FRONT_TARGET})

add_library(${PBFT_INIT_LIB} PBFTInitializer.cpp ProPBFTInitializer.cpp)
target_compile_options(${PBFT_INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${PBFT_INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB}  ${TOOL_TARGET} ${LEDGER_TARGET} ${SEALER_TARGET} ${PBFT_TARGET} ${SYNC_TARGET})

add_library(${TXPOOL_INIT_LIB} TxPoolInitializer.cpp)
# target_compile_options(${TXPOOL_INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${TXPOOL_INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} ${TOOL_TARGET} ${TXPOOL_TARGET})

add_library(${INIT_LIB} Initializer.cpp)
target_compile_options(${INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
if(ETCD)
    target_link_libraries(${INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} ${FRONTSERVICE_INIT_LIB} ${TXPOOL_INIT_LIB} ${SCHEDULER_TARGET} ${STORAGE_TARGET} ${EXECUTOR_TARGET} ${LEADER_ELECTION_TARGET})
else()
    target_link_libraries(${INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} ${FRONTSERVICE_INIT_LIB} ${TXPOOL_INIT_LIB} ${SCHEDULER_TARGET} ${STORAGE_TARGET} ${EXECUTOR_TARGET} )
endif()
