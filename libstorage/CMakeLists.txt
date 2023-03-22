
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(storage ${SRC_LIST} ${HEADERS})

target_link_libraries(storage PRIVATE blockverifier ethcore)
if(APPLE)
    target_link_libraries(storage PUBLIC devcore TBB JsonCpp Boost::Serialization Boost::Thread RocksDB zdb)
else()
    target_link_libraries(storage PUBLIC devcore TBB JsonCpp Boost::Serialization Boost::Thread RocksDB uring zdb)
endif()
target_compile_options(storage PRIVATE -Wno-error)