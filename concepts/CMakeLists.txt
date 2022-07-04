cmake_minimum_required(VERSION 3.20)
project(bcos-concepts VERSION ${VERSION})

add_library(bcos-concepts INTERFACE)
target_include_directories(bcos-concepts INTERFACE .)
target_link_directories(bcos-concepts INTERFACE bcos-framework bcos-crypto)

include(GNUInstallDirs)
install(TARGETS bcos-concepts PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_LIBDIR}")