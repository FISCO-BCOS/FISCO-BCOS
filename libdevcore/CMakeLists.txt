file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(devcore ${SRC_LIST} ${HEADERS})

target_include_directories(devcore PRIVATE ..)
target_include_directories(devcore PUBLIC ${BOOST_INCLUDE_DIR})

find_package(Eth)

add_dependencies(devcore BuildInfo.h)

target_link_libraries(devcore ${Eth_DISKENCRYPTION_LIBRARIES})

if (NOT EMSCRIPTEN)
    find_package(LevelDB)
    target_include_directories(devcore SYSTEM PUBLIC ${LEVELDB_INCLUDE_DIRS})
    
	if(BUILD_BOOST)
	target_include_directories(devcore PUBLIC ${BOOST_INCLUDE_DIR})
	target_link_libraries(devcore Boost::Filesystem Boost::Random Boost::Thread)
	target_link_libraries(devcore ssl)
	target_link_libraries(devcore leveldb)
	target_link_libraries(devcore snappy)
	target_link_libraries(devcore pthread)
	else()
	#target_link_libraries(devcore ${BOOST_LIBRARIES} ${LEVELDB_LIBRARIES})
	endif()

endif()
