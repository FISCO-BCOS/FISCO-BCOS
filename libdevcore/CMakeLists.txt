file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(devcore ${SRC_LIST} ${HEADERS})

find_package(LevelDB)
target_include_directories(devcore SYSTEM PUBLIC ${LEVELDB_INCLUDE_DIRS})
target_include_directories(devcore PUBLIC ${BOOST_INCLUDE_DIR})
target_link_libraries(devcore PUBLIC ${LEVELDB_LIBRARIES} Boost::Filesystem Boost::Thread dl)
target_link_libraries(devcore INTERFACE security)
add_dependencies(devcore BuildInfo.h)
