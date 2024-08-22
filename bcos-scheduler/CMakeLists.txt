cmake_minimum_required(VERSION 3.14)


project(bcos-scheduler VERSION ${VERSION})

aux_source_directory(src SRCS)

add_library(${SCHEDULER_TARGET} ${SRCS})
target_link_libraries(${SCHEDULER_TARGET} PUBLIC ${TABLE_TARGET} ${TARS_PROTOCOL_TARGET})
set_target_properties(${SCHEDULER_TARGET} PROPERTIES UNITY_BUILD "ON")

if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

#if (COVERAGE)
#    include(Coverage)
#    config_coverage("scheduler-coverage" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
#endif ()