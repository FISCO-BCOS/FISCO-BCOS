aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(ethcore ${SRC_LIST} ${HEADERS})

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
					

target_include_directories(ethcore PRIVATE ..)
target_include_directories(ethcore PRIVATE ../utils)
target_link_libraries(ethcore ${Dev_DEVCORE_LIBRARIES})
target_link_libraries(ethcore ${Dev_DEVCRYPTO_LIBRARIES})

install(TARGETS ethcore RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)

