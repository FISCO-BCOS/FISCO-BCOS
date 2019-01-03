
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_executable(mini-rpc ${SRC_LIST} ${HEADERS})

target_include_directories(mini-rpc PRIVATE ..)
target_link_libraries(mini-rpc PUBLIC initializer)