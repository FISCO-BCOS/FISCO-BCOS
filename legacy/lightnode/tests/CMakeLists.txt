
project(test-lightnode)

find_package(Boost REQUIRED unit_test_framework)

add_executable(test-lightnode TransactionPoolTest.cpp main.cpp)
# main.cpp defines BOOST_TEST_MODULE; merged into a unity batch the boost.test
# entry macro is disabled (headers already included), breaking the link with
# undefined init_unit_test_suite.
set_source_files_properties("main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
target_link_libraries(test-lightnode PUBLIC bcos-lightnode table protocol-tars Boost::unit_test_framework)

add_test(NAME test-lightnode COMMAND test-lightnode)