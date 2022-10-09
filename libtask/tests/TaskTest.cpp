#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <tbb/task_group.h>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <thread>

using namespace bcos::task;

struct TaskFixture
{
    tbb::task_group taskGroup;
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
    // auto task = nothingTask();

    bool finished = false;

    bcos::task::wait(
        level1(), [&finished]([[maybe_unused]] std::exception_ptr exception = nullptr) {
            std::cout << "Callback called!" << std::endl;
            finished = true;
        });
    BOOST_CHECK_EQUAL(finished, true);

    auto num = bcos::task::syncWait(level2());
    BOOST_CHECK_EQUAL(num, 10000);
}

Task<int> asyncLevel2(tbb::task_group& taskGroup)
{
    struct Awaitable
    {
        constexpr bool await_ready() const { return false; }

        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            std::cout << "Start run async thread: " << handle.address() << std::endl;
            taskGroup.run([this, m_handle = std::move(handle)]() {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                num = 100;

                std::cout << "Call m_handle.resume(): " << m_handle.address() << std::endl;
                auto handle = const_cast<decltype(m_handle)&>(m_handle);
                handle.resume();
            });
        }

        int await_resume() const
        {
            std::cout << "Call await_resume()" << std::endl;
            return num;
        }

        tbb::task_group& taskGroup;
        int num = 0;
    };

    std::cout << "co_await Awaitable started" << std::endl;
    auto num = co_await Awaitable{taskGroup, 0};
    std::cout << "co_await Awaitable ended" << std::endl;

    BOOST_CHECK_EQUAL(num, 100);

    std::cout << "asyncLevel2 co_return" << std::endl;
    co_return num;
}

Task<int> asyncLevel1(tbb::task_group& taskGroup)
{
    std::cout << "co_await asyncLevel2 started" << std::endl;
    auto num1 = co_await asyncLevel2(taskGroup);
    std::cout << "co_await asyncLevel2 ended" << std::endl;

    BOOST_CHECK_EQUAL(num1, 100);

    std::cout << "AsyncLevel1 execute finished" << std::endl;
    co_return num1 * 2;
}

BOOST_AUTO_TEST_CASE(asyncTask)
{
    auto num = bcos::task::syncWait(asyncLevel1(taskGroup));
    BOOST_CHECK_EQUAL(num, 200);

    bcos::task::wait(asyncLevel1(taskGroup), [](auto&& result) {
        using ResultType = std::remove_cvref_t<decltype(result)>;
        if constexpr (std::is_same_v<ResultType, std::exception_ptr>)
        {
            // nothing to do
        }
        else
        {
            BOOST_CHECK_EQUAL(result, 200);
            std::cout << "Got async result" << std::endl;
        }
    });
    std::cout << "Top task destroyed" << std::endl;

    taskGroup.wait();
    std::cout << "asyncTask test over" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()