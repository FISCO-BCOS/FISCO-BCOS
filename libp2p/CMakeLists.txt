set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(p2p ${SRC_LIST} ${HEADERS})

eth_use(p2p OPTIONAL Miniupnpc)

find_package(Dev)
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

target_include_directories(p2p PRIVATE ..)
target_link_libraries(p2p ${Dev_DEVCORE_LIBRARIES})
target_link_libraries(p2p ${Dev_DEVCRYPTO_LIBRARIES})

install(TARGETS p2p RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
