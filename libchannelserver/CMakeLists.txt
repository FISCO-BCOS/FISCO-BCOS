set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(channelserver ${SRC_LIST} ${HEADERS})

target_include_directories(channelserver PRIVATE ..)
target_link_libraries(channelserver PRIVATE rpc p2p stat JsonRpcCpp::Server Boost::Thread)
eth_use(channelserver OPTIONAL OpenSSL)
