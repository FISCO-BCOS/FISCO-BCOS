set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(blockverifier ${SRC_LIST} ${HEADERS})

target_include_directories(blockverifier PRIVATE ..)
target_link_libraries(blockverifier PUBLIC executivecontext devcore TBB)
target_link_libraries(blockverifier PRIVATE storage precompiled)
