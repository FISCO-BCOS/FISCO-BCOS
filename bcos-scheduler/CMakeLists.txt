cmake_minimum_required(VERSION 3.15)

include(Version)
project(bcos-scheduler VERSION ${VERSION})

aux_source_directory(src SRCS)

add_library(${SCHEDULER_TARGET} ${SRCS})
target_link_libraries(${SCHEDULER_TARGET} PUBLIC ${TABLE_TARGET})

if(TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if (COVERAGE)
    include(Coverage)
    config_coverage("coverage" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
endif ()