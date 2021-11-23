#include <tbb/task_group.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <future>

namespace bcos::test
{
struct TaskGroupFixture
{
    TaskGroupFixture() {}

    tbb::task_group taskGroup;
};

BOOST_FIXTURE_TEST_SUITE(TestTaskGroup, TaskGroupFixture)

BOOST_AUTO_TEST_CASE(newAndCall)
{
    std::atomic_size_t count = 0;
    for (size_t i = 0; i < 10000; ++i)
    {
        taskGroup.run([&count]() { ++count; });
    }

    taskGroup.wait();

    BOOST_CHECK_EQUAL(count, 10000);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test