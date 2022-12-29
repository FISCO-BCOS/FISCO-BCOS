#include <bcos-task/TBBScheduler.h>
#include <bcos-task/Task.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::task;

struct TestTBBSchedulerFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestTBBScheduler, TestTBBSchedulerFixture)

BOOST_AUTO_TEST_CASE(normalTask)
{
    TBBScheduler scheduler;
    scheduler.run([]() -> Task<void> { co_return; }());
    // auto task = nothingTask();
    bool finished = false;
}

BOOST_AUTO_TEST_SUITE_END()