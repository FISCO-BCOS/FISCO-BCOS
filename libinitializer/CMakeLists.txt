set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(initializer ${SRC_LIST} ${HEADERS})

#find_package(Boost QUIET REQUIRED COMPONENTS)

target_include_directories(initializer PRIVATE ..)

target_link_libraries(initializer ${Dev_DEVCORE_LIBRARIES})
target_link_libraries(initializer ${Dev_DEVCRYPTO_LIBRARIES})
target_link_libraries(initializer ${Eth_ETHCORE_LIBRARIES})
target_link_libraries(initializer ${Eth_ETHEREUM_LIBRARIES})
target_link_libraries(initializer pbftseal)
target_link_libraries(initializer storage)
target_link_libraries(initializer precompiled)
target_link_libraries(initializer channelserver)
target_link_libraries(initializer web3jsonrpc)
target_link_libraries(initializer console)
