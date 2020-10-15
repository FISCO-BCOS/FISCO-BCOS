set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(rpc ${SRC_LIST} ${HEADERS})
target_include_directories(rpc PRIVATE ..)
target_link_libraries(rpc PUBLIC flowlimit ledger consensus JsonRpcCpp::Server Boost::iostreams initializer)
