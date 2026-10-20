

project(bcos-scheduler VERSION ${VERSION})

aux_source_directory(src SRCS)

add_library(scheduler ${SRCS})
target_include_directories(scheduler PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(scheduler PUBLIC ledger table protocol-tars)

if(TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
