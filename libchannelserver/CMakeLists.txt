set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(channelserver ${SRC_LIST} ${HEADERS})

target_include_directories(channelserver PRIVATE ..)
target_include_directories(channelserver PUBLIC ${BOOST_INCLUDE_DIR})
eth_use(channelserver OPTIONAL OpenSSL)
target_link_libraries(channelserver JsonRpcCpp::Server Boost::Thread)

install(TARGETS channelserver RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
