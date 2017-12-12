file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

include(EthDependencies)

add_library(statistics ${SRC_LIST} ${HEADERS})
target_include_directories(statistics PUBLIC ${BOOST_INCLUDE_DIR})
target_link_libraries(statistics JsonCpp JsonRpcCpp::Server microhttpd)

find_package(Dev)

target_include_directories(statistics PRIVATE ..)
