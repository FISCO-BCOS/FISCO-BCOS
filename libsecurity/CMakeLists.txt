file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(security ${SRC_LIST} ${HEADERS})
target_include_directories(security PUBLIC ${BOOST_INCLUDE_DIR})
target_include_directories(security SYSTEM PUBLIC ${LEVELDB_INCLUDE_DIRS})
target_link_libraries(security PRIVATE devcore devcrypto Boost::Filesystem Boost::Thread ${LEVELDB_LIBRARIES} ${JSONCPP_LIBRARY})

