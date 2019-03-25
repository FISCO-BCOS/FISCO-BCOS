
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(compress ${SRC_LIST} ${HEADERS})
target_link_libraries(compress PUBLIC devcore Snappy)
