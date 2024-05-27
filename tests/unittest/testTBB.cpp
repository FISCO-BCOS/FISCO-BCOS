#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <iostream>

struct TBBFixture
{
    tbb::task_group taskGroup;
};

BOOST_FIXTURE_TEST_SUITE(TBBTest, TBBFixture)

BOOST_AUTO_TEST_CASE(pointerTest)
{
    auto future = std::async([]() {
        std::cout << "Main future started!" << std::endl;
        int a = 100;
        int b = 200;
        oneapi::tbb::task::suspend([&](oneapi::tbb::task::suspend_point tag) {
            auto future = std::async([&, tag]() {
                std::cout << "Sub future started!" << std::endl;

                std::this_thread::sleep_for(std::chrono::seconds(3));
                std::cout << "A: " << a << std::endl;

                tbb::task::resume(tag);

                std::this_thread::sleep_for(std::chrono::seconds(3));
                std::cout << "Sub future ended!" << std::endl;
            });
        });

        std::cout << "Main future ended!" << std::endl;
    });

    future.wait();
}

BOOST_AUTO_TEST_CASE(resumeableTaskTest)
{
    tbb::parallel_for(tbb::blocked_range<size_t>(0, 100), [](auto const& range) {
        for (auto i = range.begin(); i != range.end(); ++i)
        {
            std::cout << std::this_thread::get_id() << " suspend now!" << std::endl;
            oneapi::tbb::task::suspend([&](oneapi::tbb::task::suspend_point tag) {
                auto future = std::async([tag]() {
                    std::cout << std::this_thread::get_id() << " waiting to resume!" << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                    std::cout << std::this_thread::get_id() << " resume now!" << std::endl;
                    tbb::task::resume(tag);
                    std::cout << std::this_thread::get_id() << " I am still there!" << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                    std::cout << std::this_thread::get_id() << " I die now!" << std::endl;
                });
            });
            std::cout << std::this_thread::get_id() << " suspend is over!" << std::endl;
        }
    });

    std::cout << std::this_thread::get_id() << " parallel_for is over!" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()