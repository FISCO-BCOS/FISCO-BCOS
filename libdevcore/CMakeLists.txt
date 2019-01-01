file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(devcore ${SRC_LIST} ${HEADERS})

find_package(LevelDB)
target_include_directories(devcore SYSTEM PUBLIC ${LEVELDB_INCLUDE_DIRS})
target_include_directories(devcore SYSTEM BEFORE PUBLIC ${BOOST_INCLUDE_DIR})
target_link_libraries(devcore PRIVATE security)
target_link_libraries(devcore PUBLIC Boost::Filesystem Boost::Thread ${LEVELDB_LIBRARIES})
add_dependencies(devcore BuildInfo.h)
