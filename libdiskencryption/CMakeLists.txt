file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(diskencryption ${SRC_LIST} ${HEADERS})
target_include_directories(diskencryption PUBLIC ${BOOST_INCLUDE_DIR})
target_include_directories(diskencryption SYSTEM PUBLIC ${LEVELDB_INCLUDE_DIRS})
target_link_libraries(diskencryption PRIVATE devcore devcrypto Boost::Filesystem Boost::Thread ${LEVELDB_LIBRARIES} JsonRpcCpp::Client)

