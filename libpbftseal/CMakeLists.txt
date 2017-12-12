aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(pbftseal ${SRC_LIST} ${HEADERS})

find_package(Eth)
find_package(Dev)

target_include_directories(pbftseal PRIVATE ..)

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

target_link_libraries(pbftseal ${Dev_DEVCORE_LIBRARIES})
target_link_libraries(pbftseal ${Dev_DEVCRYPTO_LIBRARIES})
target_link_libraries(pbftseal ${Eth_ETHCORE_LIBRARIES})
target_link_libraries(pbftseal ${Eth_ETHEREUM_LIBRARIES})

install(TARGETS pbftseal RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
