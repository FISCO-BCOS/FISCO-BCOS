#include <tbb/task_group.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <future>

namespace bcos::test
{
struct SchedulerExecutorFixture
{
    SchedulerExecutorFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(TestSchedulerExecutor, SchedulerExecutorFixture)

BOOST_AUTO_TEST_CASE(parallelExecutor) {}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test