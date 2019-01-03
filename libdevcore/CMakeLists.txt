file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(devcore ${SRC_LIST} ${HEADERS})

find_package(LevelDB)
target_include_directories(devcore SYSTEM PUBLIC ${LEVELDB_INCLUDE_DIRS})
target_include_directories(devcore SYSTEM BEFORE PUBLIC ${BOOST_INCLUDE_DIR})
# set_source_files_properties(${SRC_LIST} ${HEADERS} PROPERTIES COMPILE_FLAGS -Wno-unused-function)
target_compile_options(devcore BEFORE PRIVATE -Wno-error -Wno-unused-variable -Wno-unused-function -Wno-uninitialized -Wno-all)
target_link_libraries(devcore PUBLIC ${LEVELDB_LIBRARIES} Boost::Filesystem Boost::Thread Boost::Log)
# target_link_libraries(devcore INTERFACE security)
add_dependencies(devcore BuildInfo.h)
