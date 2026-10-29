
find_package(Boost REQUIRED unit_test_framework)
find_package(TBB REQUIRED)

add_executable(test-task TestTask.cpp TestFireAwaitable.cpp TestDeadline.cpp TestImmediate.cpp
	TestOneShot.cpp main.cpp)
# main.cpp defines BOOST_TEST_MODULE; when merged into a unity batch, boost.test
# headers are already included by earlier files and the entry macro is disabled,
# leaving the linker without init_unit_test_suite.
set_source_files_properties("main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
target_link_libraries(
	test-task PUBLIC bcos-task bcos-utilities Boost::unit_test_framework TBB::tbb)

add_test(NAME test-task COMMAND test-task)

foreach(test_name IN ITEMS test-task-generator test-task-generator-fallback)
    add_executable(${test_name} TestGenerator.cpp main.cpp)
    target_link_libraries(${test_name} PRIVATE bcos-task Boost::unit_test_framework)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
target_compile_definitions(
    test-task-generator-fallback PRIVATE BCOS_TASK_FORCE_GENERATOR_FALLBACK)
