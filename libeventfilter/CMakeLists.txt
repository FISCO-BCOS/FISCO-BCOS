file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(eventfilter ${SRC_LIST} ${HEADERS})

target_link_libraries(eventfilter PUBLIC utilities ethcore JsonCpp)
