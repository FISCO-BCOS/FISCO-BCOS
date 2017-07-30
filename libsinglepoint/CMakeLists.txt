aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")


set(SRC_LIST ${SRC_LIST} ${SRCS})

add_library(singlepoint ${SRC_LIST} ${HEADERS})
eth_use(singlepoint OPTIONAL Eth::singlepoint-cl Cpuid)

find_package(Eth)

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
target_include_directories(singlepoint PRIVATE ..)
target_link_libraries(singlepoint ${Eth_ETHEREUM_LIBRARIES})

install(TARGETS singlepoint RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
