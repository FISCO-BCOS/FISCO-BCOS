set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(flowlimit ${SRC_LIST} ${HEADERS})

target_link_libraries(flowlimit PUBLIC utilities)
