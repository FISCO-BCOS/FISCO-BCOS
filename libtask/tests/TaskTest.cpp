#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::task;

struct TaskFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TaskTest, TaskFixture)

Task<void> nothingTask()
{
    BOOST_FAIL("No expect to run!");
    co_return;
}

Task<int> level3()
{
    std::cout << "Level3 execute finished" << std::endl;
    co_return 100;
}

Task<long> level2()
{
    auto numResult = co_await level3();
    BOOST_CHECK_EQUAL(numResult, 100);

    constexpr static auto mut = 100L;

    std::cout << "Level2 execute finished" << std::endl;
    co_return static_cast<long>(numResult) * mut;
}

Task<void> level1()
{
    auto num1 = co_await level3();
    auto num2 = co_await level2();

    BOOST_CHECK_EQUAL(num1, 100);
    BOOST_CHECK_EQUAL(num2, 10000);

    std::cout << "Level1 execute finished" << std::endl;
}

BOOST_AUTO_TEST_CASE(normalTask)
{
    auto task = nothingTask();

    bool finished = false;
    auto task2 = level1();

    bcos::task::wait(
        std::move(task2), [&finished]([[maybe_unused]] std::exception_ptr exception = nullptr) {
            std::cout << "Callback called!" << std::endl;
            finished = true;
        });
    BOOST_CHECK_EQUAL(finished, true);

    auto task3 = level2();
    auto num = bcos::task::syncWait(std::move(task3));
    BOOST_CHECK_EQUAL(num, 10000);
}

BOOST_AUTO_TEST_SUITE_END()