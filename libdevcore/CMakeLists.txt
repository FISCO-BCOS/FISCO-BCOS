file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(devcore ${SRC_LIST} ${HEADERS})

target_include_directories(devcore PRIVATE ..)
target_include_directories(devcore SYSTEM PUBLIC ${BOOST_INCLUDE_DIR})

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

add_dependencies(devcore BuildInfo.h)

if (NOT EMSCRIPTEN)
    find_package(LevelDB)
    target_include_directories(devcore SYSTEM PUBLIC ${LEVELDB_INCLUDE_DIRS})
    target_link_libraries(devcore Boost::Filesystem Boost::Random Boost::Thread ${LEVELDB_LIBRARIES})
endif()
