file(GLOB SRC_LIST "*.cpp")

find_package(Boost CONFIG REQUIRED program_options)
find_package(OpenSSL REQUIRED)
find_package(tarscpp CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

set(PROTOCOL_INIT_LIB protocol_init)
set(FRONTSERVICE_INIT_LIB front_init)
set(PBFT_INIT_LIB pbft_init)
set(TXPOOL_INIT_LIB txpool_init)
set(INIT_LIB init)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)
include_directories(${CMAKE_SOURCE_DIR}/bcos-txpool)
include_directories(${CMAKE_SOURCE_DIR}/bcos-front)
include_directories(${CMAKE_SOURCE_DIR}/bcos-pbft)
include_directories(${CMAKE_SOURCE_DIR}/bcos-sync)
include_directories(${CMAKE_SOURCE_DIR}/bcos-gateway)
include_directories(${CMAKE_SOURCE_DIR}/bcos-executor)
include_directories(${CMAKE_SOURCE_DIR}/bcos-ledger)
include_directories(${CMAKE_SOURCE_DIR}/bcos-scheduler)
include_directories(${CMAKE_SOURCE_DIR}/bcos-storage)
include_directories(${CMAKE_SOURCE_DIR}/bcos-rpc)
include_directories(${CMAKE_SOURCE_DIR}/fisco-bcos-pro)
include_directories(${CMAKE_BINARY_DIR})

add_library(${PROTOCOL_INIT_LIB} ProtocolInitializer.cpp)
target_compile_options(${PROTOCOL_INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${PROTOCOL_INIT_LIB} Boost::program_options OpenSSL::SSL OpenSSL::Crypto 
                     crypto protocol-tars tarscpp::tarsservant tarscpp::tarsutil)

add_library(${FRONTSERVICE_INIT_LIB} FrontServiceInitializer.cpp)
# target_compile_options(${FRONTSERVICE_INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${FRONTSERVICE_INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} front)

add_library(${PBFT_INIT_LIB} PBFTInitializer.cpp)
target_compile_options(${PBFT_INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${PBFT_INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} ledger pbft sync)

add_library(${TXPOOL_INIT_LIB} TxPoolInitializer.cpp)
# target_compile_options(${TXPOOL_INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${TXPOOL_INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB} txpool)

add_library(${INIT_LIB} ${SRC_LIST})
target_compile_options(${INIT_LIB} PRIVATE -Wno-error -Wno-unused-parameter -Wno-variadic-macros -Wno-return-type -Wno-pedantic)
target_link_libraries(${INIT_LIB} PUBLIC ${PROTOCOL_INIT_LIB}
${FRONTSERVICE_INIT_LIB} ${PBFT_INIT_LIB} ${TXPOOL_INIT_LIB} storage ledger scheduler executor)