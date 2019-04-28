file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(mptstate ${SRC_LIST} ${HEADERS})

target_link_libraries(mptstate PRIVATE ethcore security)
