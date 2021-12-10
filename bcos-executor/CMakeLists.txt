cmake_minimum_required(VERSION 3.14)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum OS X deployment version")

project(bcos-executor VERSION "3.0.0")

aux_source_directory(src SRCS)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bcos-framework)

add_library(executor ${SRCS})
target_link_libraries(executor framework TBB::tbb)

# if (TOOLS)
#     add_subdirectory(tools)
# endif()

# if (TESTS)
#     enable_testing()
#     set(ENV{CTEST_OUTPUT_ON_FAILURE} True)
#     add_subdirectory(test)
# endif()
