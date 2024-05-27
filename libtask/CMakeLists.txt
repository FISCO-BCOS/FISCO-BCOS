cmake_minimum_required(VERSION 3.17)

add_library(bcos-task INTERFACE)
target_include_directories(bcos-task INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-task>)
target_link_libraries(bcos-task INTERFACE bcos-concepts)

include(GNUInstallDirs)
install(TARGETS bcos-task EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-task" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")

if(TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()