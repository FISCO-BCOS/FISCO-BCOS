file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(storagestate ${SRC_LIST} ${HEADERS})

target_link_libraries(storagestate PUBLIC storage devcrypto)
