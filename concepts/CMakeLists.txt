cmake_minimum_required(VERSION 3.17)
project(bcos-concepts VERSION ${VERSION})

add_library(bcos-concepts INTERFACE)
target_include_directories(bcos-concepts INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-concepts>)
target_link_libraries(bcos-concepts INTERFACE bcos-framework bcos-crypto)

include(GNUInstallDirs)
install(TARGETS bcos-concepts EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")