
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(storage ${SRC_LIST} ${HEADERS})
#target_include_directories(storage PUBLIC ${TBB_INCLUDE_DIR})
target_link_libraries(storage PRIVATE blockverifier ethcore)
target_link_libraries(storage PUBLIC devcore TBB JsonCpp Boost::Thread RocksDB zdb)
