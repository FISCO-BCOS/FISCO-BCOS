aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

include(EthDependencies)

add_executable(fisco-bcos ${SRC_LIST} ${HEADERS})

find_package(Eth)
find_package(Web3)

target_include_directories(fisco-bcos PRIVATE ..)
					
target_link_libraries(fisco-bcos ${Eth_EVM_LIBRARIES})
target_link_libraries(fisco-bcos ${Web3_WEB3JSONRPC_LIBRARIES})
target_link_libraries(fisco-bcos ${Web3_WEBTHREE_LIBRARIES})
target_link_libraries(fisco-bcos ${Eth_DISKENCRYPTION_LIBRARIES})
target_link_libraries(fisco-bcos JsonRpcCpp::Client)
target_link_libraries(fisco-bcos pbftseal)
target_link_libraries(fisco-bcos contract)
target_link_libraries(fisco-bcos channelserver)
target_link_libraries(fisco-bcos web3jsonrpc)
target_link_libraries(fisco-bcos abi)

if (EVMJIT)
	# Do we need include paths here?
	target_link_libraries(fisco-bcos ${Eth_EVMJIT_LIBRARIES})
	eth_copy_dlls(eth EVMJIT_DLLS)
endif()

if (UNIX AND NOT APPLE)
	target_link_libraries(fisco-bcos pthread)
endif()

install(TARGETS fisco-bcos DESTINATION bin)
