
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(storage ${SRC_LIST} ${HEADERS})

if (USE_JNI)
FIND_PACKAGE(Java REQUIRED)
FIND_PACKAGE(JNI REQUIRED)
target_include_directories(storage PRIVATE ${JAVA_INCLUDE_PATH})
target_include_directories(storage PRIVATE ${JAVA_INCLUDE_PATH2})
target_link_libraries(storage PRIVATE ${JNI_LIBRARIES})
endif ()

target_link_libraries(storage PRIVATE blockverifier ethcore)
target_link_libraries(storage PUBLIC devcore TBB JsonCpp Boost::Thread RocksDB)
