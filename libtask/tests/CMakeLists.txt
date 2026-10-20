
find_package(Boost REQUIRED unit_test_framework)
find_package(TBB REQUIRED)

add_executable(test-task TestTask.cpp TestFireAwaitable.cpp TestChannel.cpp TestDeadline.cpp TestImmediate.cpp
	TestOneShot.cpp main.cpp)
# main.cpp defines BOOST_TEST_MODULE; when merged into a unity batch, boost.test
# headers are already included by earlier files and the entry macro is disabled,
# leaving the linker without init_unit_test_suite.
set_source_files_properties("main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
target_link_libraries(
	test-task PUBLIC bcos-task bcos-utilities Boost::unit_test_framework TBB::tbb)

add_test(NAME test-task COMMAND test-task)