# MultiVersionScheduler routing test. MultiVersionScheduler.cpp is compiled DIRECTLY into the
# test target (round-3 C4): the `initializer` target does not exist (the real lib is `init`, which
# drags ~30 libs). MultiVersionScheduler.cpp only depends on MultiVersionScheduler.h (->
# SchedulerInterface.h) + Common.h (BoostLog), so direct compilation needs no full dep graph.
find_package(Boost REQUIRED unit_test_framework)
add_executable(libinitializer-tests MultiVersionSchedulerTest.cpp ../MultiVersionScheduler.cpp)
set_target_properties(libinitializer-tests PROPERTIES UNITY_BUILD OFF)
target_link_libraries(libinitializer-tests PRIVATE bcos-framework Boost::unit_test_framework)
# 测试 include <libinitializer/MultiVersionScheduler.h>（按仓库根相对），需仓库根在 include 路径上
target_include_directories(libinitializer-tests PRIVATE ${PROJECT_SOURCE_DIR})
add_test(NAME LibinitializerTests COMMAND libinitializer-tests)
