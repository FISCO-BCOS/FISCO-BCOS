aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")


include(EthDependencies)

add_executable(eth ${SRC_LIST} ${HEADERS})

find_package(Eth)
find_package(Web3)

target_include_directories(eth PRIVATE ..)

include_directories(../libodbc/include
					../libodbc/include/db2_linux
					../libodbc/include/ibase
					../libodbc/include/infomix
					../libodbc/include/mysql
					../libodbc/include/odbc_linux
					../libodbc/include/pgsql
					../libodbc/include/sqlbase
					../libodbc/include/sqllite
					../libodbc/include/ss_linux
					../libodbc/include/sybase)
					
target_link_libraries(eth ${Eth_EVM_LIBRARIES})
target_link_libraries(eth ${Web3_WEB3JSONRPC_LIBRARIES})
target_link_libraries(eth ${Web3_WEBTHREE_LIBRARIES})
target_link_libraries(eth contract)
target_link_libraries(eth JsonRpcCpp::Client)

if (EVMJIT)
	# Do we need include paths here?
	target_link_libraries(eth ${Eth_EVMJIT_LIBRARIES})
	eth_copy_dlls(eth EVMJIT_DLLS)
endif()

if (UNIX AND NOT APPLE)
	target_link_libraries(eth pthread)
endif()

install(TARGETS eth DESTINATION bin)
