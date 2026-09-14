file(GLOB_RECURSE SOURCES "*.cpp")

# Single-TU suites only: MultiVersionSchedulerTest defines BOOST_TEST_MODULE (auto main);
# a second TU defining a module would collide. MultiVersionScheduler.cpp is compiled in
# directly so the test does not link the init mega-target.
add_executable(test-libinitializer ${SOURCES} ../MultiVersionScheduler.cpp)
find_package(Boost REQUIRED unit_test_framework)
target_include_directories(test-libinitializer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test-libinitializer
    PRIVATE bcos-framework bcos-task bcos-utilities Boost::unit_test_framework)
