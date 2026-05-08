file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

find_package(tarscpp CONFIG REQUIRED)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)

add_executable(echo_client_sample echo_client_sample.cpp)
target_link_libraries(echo_client_sample PUBLIC tool gateway protocol-tars bcos-crypto bcos-utilities)


add_executable(echo_server_sample echo_server_sample.cpp)
target_link_libraries(echo_server_sample PUBLIC tool gateway protocol-tars bcos-crypto bcos-utilities)