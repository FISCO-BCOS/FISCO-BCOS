set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(sync ${SRC_LIST} ${HEADERS})

target_link_libraries(sync PUBLIC blockchain txpool p2p TCMalloc)
