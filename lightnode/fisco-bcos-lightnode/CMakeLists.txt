cmake_minimum_required(VERSION 3.17)

add_executable(fisco-bcos-lightnode main.cpp)
target_link_libraries(fisco-bcos-lightnode PUBLIC bcos-lightnode)

if(TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()