set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(txpool ${SRC_LIST} ${HEADERS})
target_link_libraries(txpool PUBLIC ethcore devcore TBB)

