# Compile MultiVersionScheduler.cpp here; the init library pulls too many deps.
find_package(Boost REQUIRED unit_test_framework)
add_executable(libinitializer-tests MultiVersionSchedulerTest.cpp ../MultiVersionScheduler.cpp)
set_target_properties(libinitializer-tests PROPERTIES UNITY_BUILD OFF)
target_link_libraries(libinitializer-tests PRIVATE bcos-framework Boost::unit_test_framework)
target_include_directories(libinitializer-tests PRIVATE ${PROJECT_SOURCE_DIR})
add_test(NAME LibinitializerTests COMMAND libinitializer-tests)
