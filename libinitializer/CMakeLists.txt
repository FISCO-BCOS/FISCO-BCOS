file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

#add_library(initializer ${SRC_LIST} ${HEADERS})

add_library(${PROTOCOL_INIT_LIB} ProtocolInitializer.cpp)
target_compile_options(${PROTOCOL_INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${PROTOCOL_INIT_LIB} Boost::program_options bcos-framework::utilities bcos-framework::codec bcos-framework::protocol bcos-framework::tool OpenSSL::SSL OpenSSL::Crypto 
                     bcos-framework::protocol bcos-crypto::bcos-crypto bcos-tars-protocol::protocol-tars tarscpp::tarsservant tarscpp::tarsutil)

add_library(${FRONTSERVICE_INIT_LIB} FrontServiceInitializer.cpp)
target_compile_options(${FRONTSERVICE_INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${FRONTSERVICE_INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} bcos-framework::tool bcos-front::bcos-front)

add_library(${PBFT_INIT_LIB} PBFTInitializer.cpp)
target_compile_options(${PBFT_INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${PBFT_INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} bcos-ledger::bcos-ledger bcos-framework::tool bcos-framework::sealer bcos-pbft::bcos-pbft bcos-pbft::bcos-consensus-core bcos-sync::block-sync)

add_library(${TXPOOL_INIT_LIB} TxPoolInitializer.cpp)
target_compile_options(${TXPOOL_INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${TXPOOL_INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} bcos-framework::tool bcos-txpool::bcos-txpool)

add_library(${INIT_LIB} ${SRC_LIST} ${HEADERS})
target_compile_options(${INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} ${FRONTSERVICE_INIT_LIB} ${PBFT_INIT_LIB} ${TXPOOL_INIT_LIB} bcos-storage::bcos-storage bcos-ledger::bcos-ledger bcos-scheduler::scheduler bcos::executor)