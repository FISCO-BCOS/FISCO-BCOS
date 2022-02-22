#include <oneapi/tbb/parallel_for.h>
#include <tbb/task.h>
#include <tbb/task_group.h>
#include <tbb/tbb_allocator.h>
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
    for (size_t i = 0; i < 1000; ++i)
    {
        taskGroup.run([&count]() { ++count; });
    }

    taskGroup.wait();

    BOOST_CHECK_EQUAL(count, 1000);
}

BOOST_AUTO_TEST_CASE(nestTask) {}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test