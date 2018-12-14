set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(channelserver ${SRC_LIST} ${HEADERS})

eth_use(channelserver OPTIONAL OpenSSL)
target_include_directories(channelserver PRIVATE ..)
target_include_directories(channelserver PUBLIC ${BOOST_INCLUDE_DIR})

target_link_libraries(channelserver PUBLIC JsonRpcCpp::Server Boost::Thread)
add_dependencies(channelserver Boost::Thread)


install(TARGETS channelserver RUNTIME DESTINATION bin ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
