#------------------------------------------------------------------------------
# Top-level CMake file for bcos-mempool
# ------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.10)

project(bcos-mempool VERSION ${VERSION})

add_subdirectory(bcos-mempool)

if (TESTS)
	enable_testing()
	set(CTEST_OUTPUT_ON_FAILURE TRUE)
	add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(DIRECTORY "bcos-mempool" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")