file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(security ${SRC_LIST} ${HEADERS})
target_link_libraries(security PUBLIC JsonCpp)
target_link_libraries(security PRIVATE devcrypto storage)
