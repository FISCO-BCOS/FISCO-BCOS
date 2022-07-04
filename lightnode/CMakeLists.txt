cmake_minimum_required(VERSION 3.20)
project(lightnode VERSION ${VERSION})

add_executable(lightnode main.cpp)
target_link_libraries(lightnode PUBLIC bcos-concepts bcos-framework bcos-crypto)

if(TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()