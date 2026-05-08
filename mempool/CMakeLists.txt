#------------------------------------------------------------------------------
# Top-level CMake file for bcos-mempool
# ------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.10)

project(bcos-mempool VERSION ${VERSION})

file(GLOB_RECURSE SRC_LIST "bcos-mempool/*.cpp")

find_package(TBB CONFIG REQUIRED)

add_library(mempool ${SRC_LIST} ${HEADERS})
target_include_directories(mempool PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-mempool>)
target_link_libraries(mempool PUBLIC bcos-framework ledger bcos-utilities TBB::tbb)
set_target_properties(mempool PROPERTIES UNITY_BUILD "ON")

if (TESTS)
	enable_testing()
	set(CTEST_OUTPUT_ON_FAILURE TRUE)
	add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS mempool EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-mempool" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")