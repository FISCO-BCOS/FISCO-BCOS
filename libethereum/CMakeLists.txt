file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

include(EthDependencies)

add_library(ethereum ${SRC_LIST} ${HEADERS})

target_include_directories(ethereum PRIVATE ${BOOST_INCLUDE_DIR})
#target_link_libraries(ethereum Boost::Regex JsonCpp)

find_package(Dev)
find_package(Eth)

target_include_directories(ethereum PRIVATE ..)
target_include_directories(ethereum PRIVATE ../utils)
					
target_link_libraries(ethereum ${Eth_ETHCORE_LIBRARIES})
target_link_libraries(ethereum ${Eth_EVM_LIBRARIES})
target_link_libraries(ethereum precompiled)
target_link_libraries(ethereum storage)
target_link_libraries(ethereum initializer)
target_link_libraries(ethereum ${Eth_DISKENCRYPTION_LIBRARIES})
target_link_libraries(ethereum UTXO)
target_link_libraries(ethereum leveldb)
target_link_libraries(ethereum snappy)
target_link_libraries(ethereum pthread)

if (NOT EMSCRIPTEN)
	target_link_libraries(ethereum ${Dev_P2P_LIBRARIES})
	target_link_libraries(ethereum ${Dev_DEVCRYPTO_LIBRARIES})
endif()

if (EVMJIT)
	target_link_libraries(ethereum ${Eth_EVMJIT_LIBRARIES})
endif()
