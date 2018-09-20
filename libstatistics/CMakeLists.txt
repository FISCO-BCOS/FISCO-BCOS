file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

include(EthDependencies)

add_library(statistics ${SRC_LIST} ${HEADERS})
target_include_directories(statistics PUBLIC ${BOOST_INCLUDE_DIR})
target_include_directories(statistics PUBLIC ${MHD_INCLUDE_DIR})

target_link_libraries(statistics PUBLIC JsonCpp)
target_link_libraries(statistics PUBLIC JsonRpcCpp::Server)
target_link_libraries(statistics PUBLIC ${MHD_LIBRARY})

find_package(Dev)

target_include_directories(statistics PRIVATE ..)