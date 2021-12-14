set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(${WS_TARGET} ${SRC_LIST} ${HEADERS})

target_link_libraries(${WS_TARGET} PUBLIC jsoncpp_lib_static bcos-framework::utilities)
