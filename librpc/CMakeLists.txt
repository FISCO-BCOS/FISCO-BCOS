set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(rpc ${SRC_LIST} ${HEADERS})
target_include_directories(rpc PRIVATE ..)
target_link_libraries(rpc PUBLIC JsonRpcCpp::Server)
target_link_libraries(rpc PRIVATE ledger p2p)
