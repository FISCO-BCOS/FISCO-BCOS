set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")
list(APPEND SRC_LIST libexecstream/exec-stream.cpp)

add_library(webthree ${SRC_LIST} ${HEADERS})

eth_use(webthree REQUIRED Web3::whisper)

find_package(Eth)
find_package(Web3)
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

target_include_directories(webthree PRIVATE ..)
#target_link_libraries(webthree ${Eth_ETHASH_LIBRARIES})
#target_link_libraries(webthree ${Eth_ETHASHSEAL_LIBRARIES})
target_link_libraries(webthree ${Eth_SINGLEPOINT_LIBRARIES})
target_link_libraries(webthree ${Eth_PBFTSEAL_LIBRARIES})
target_link_libraries(webthree ${Eth_RAFTSEAL_LIBRARIES})
target_link_libraries(webthree ${Web3_WHISPER_LIBRARIES})

install(TARGETS webthree RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
