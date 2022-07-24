cmake_minimum_required(VERSION 3.17)
project(lightnode VERSION ${VERSION})

add_library(bcos-lightnode INTERFACE)
target_include_directories(bcos-lightnode INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bcos-lightnode INTERFACE bcos-framework bcos-crypto)

add_executable(lightnode main.cpp)
target_link_libraries(lightnode PUBLIC bcos-concepts bcos-lightnode)

if(TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()