cmake_minimum_required(VERSION 3.14)


project(bcos-scheduler VERSION ${VERSION})

aux_source_directory(src SRCS)

add_library(scheduler ${SRCS})
target_link_libraries(scheduler PUBLIC ledger table protocol-tars)
set_target_properties(scheduler PROPERTIES UNITY_BUILD "ON")

if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
