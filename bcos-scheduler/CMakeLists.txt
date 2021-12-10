cmake_minimum_required(VERSION 3.15)

project(bcos-scheduler VERSION "3.0.0")

aux_source_directory(bcos-scheduler SRCS)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/bcos-scheduler)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

find_package(TBB CONFIG REQUIRED)

add_library(scheduler ${SRCS})
target_link_libraries(scheduler framework TBB::tbb)

# if(TESTS)
#     enable_testing()
#     add_subdirectory(tests)
# endif()

if (COVERAGE)
    include(Coverage)
    config_coverage("coverage" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()