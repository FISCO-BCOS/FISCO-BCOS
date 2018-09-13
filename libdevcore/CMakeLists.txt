file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(devcore ${SRC_LIST} ${HEADERS})
target_include_directories(devcore PUBLIC ${BOOST_INCLUDE_DIR})

add_dependencies(devcore BuildInfo.h)

find_package(LevelDB)
target_include_directories(devcore SYSTEM PUBLIC ${LEVELDB_INCLUDE_DIRS})
target_link_libraries(devcore PRIVATE Boost::Filesystem Boost::Thread ${LEVELDB_LIBRARIES})
