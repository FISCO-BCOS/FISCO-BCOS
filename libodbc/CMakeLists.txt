#设置ODBC-PATH
set(ODBC_PATH /usr/local/app/sqlapi/lib64)

#添加目录下所有源文件
aux_source_directory(. ODBC_SRC_LIST)

#递归寻找头文件
file(GLOB_RECURSE HEADERS "*.h")


#静态库
add_library(odbc STATIC ${ODBC_SRC_LIST} ${HEADERS})

find_package(Eth)
find_package(Dev)

target_include_directories(odbc PRIVATE ..)
target_include_directories(odbc PRIVATE ./include
					./include/db2_linux
					./include/ibase
					./include/infomix
					./include/mysql
					./include/odbc_linux
					./include/pgsql
					./include/sqlbase
					./include/sqllite
					./include/ss_linux
					./include/sybase)
					
target_include_directories(odbc PRIVATE ../utils)
link_directories(${ODBC_PATH})
target_link_libraries(odbc ${Dev_DEVCORE_LIBRARIES})
target_link_libraries(odbc ${Boost_REGEX_LIBRARIES} JsonCpp)
target_link_libraries(odbc kayakDBOp)
target_link_libraries(odbc dl)


install(TARGETS odbc RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
