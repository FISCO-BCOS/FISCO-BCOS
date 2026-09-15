file(GLOB_RECURSE SOURCES "*.cpp")

# Single-TU suites only: MultiVersionSchedulerTest defines BOOST_TEST_MODULE (auto main);
# a second TU defining a module would collide. MultiVersionScheduler.cpp is compiled in
# directly so the test does not link the init mega-target.
add_executable(test-libinitializer ${SOURCES} ../MultiVersionScheduler.cpp)
find_package(Boost REQUIRED unit_test_framework)
target_include_directories(test-libinitializer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test-libinitializer
    PRIVATE bcos-framework bcos-task bcos-utilities Boost::unit_test_framework)

# Register explicitly: without add_test the suite is compiled and never executed by the CI
# ctest run, so the behaviour it pins (the setVersion freeze keying, and the shutdown sweep
# skipping unwired slots) would have no signal. The registration only reaches ctest because
# ../CMakeLists.txt calls enable_testing() before add_subdirectory(tests) — the parent chain has
# to enable testing for this file to be linked into its parent's CTestTestfile.cmake.
add_test(NAME test-libinitializer
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMAND test-libinitializer)
