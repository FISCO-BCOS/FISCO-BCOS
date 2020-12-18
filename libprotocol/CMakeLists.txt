file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(protocol ${SRC_LIST} ${HEADERS})

target_link_libraries(protocol PUBLIC devcrypto TBB)
