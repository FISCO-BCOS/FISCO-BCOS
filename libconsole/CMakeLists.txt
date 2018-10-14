set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

add_library(console ${SRC_LIST} ${HEADERS})

target_include_directories(console PRIVATE ..)
target_include_directories(console PUBLIC ${BOOST_INCLUDE_DIR})

target_link_libraries(console JsonRpcCpp::Server)
target_link_libraries(console dl)