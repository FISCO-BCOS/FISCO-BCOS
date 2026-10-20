
project(FISCO-BCOS-Test)

file(GLOB_RECURSE SOURCES "unittest/*.cpp")

find_package(TBB CONFIG REQUIRED)

add_executable(fisco-bcos-test ${SOURCES})
# unittest/main.cpp defines BOOST_TEST_MODULE; merged into a unity batch the
# boost.test entry macro is disabled (headers already included), breaking the link
# with undefined init_unit_test_suite.
set_source_files_properties("unittest/main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
find_package(Boost REQUIRED unit_test_framework program_options)
target_link_libraries(fisco-bcos-test PUBLIC tool bcos-crypto protocol-tars Boost::program_options Boost::unit_test_framework TBB::tbb)

# add_test(NAME fisco-bcos-test WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND fisco-bcos-test)

add_subdirectory(perf)