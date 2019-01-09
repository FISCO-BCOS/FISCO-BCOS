/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief : unit test of parallel transactions executor
 * @file: ParaTxExecutorTest.cpp
 * @author: catli
 * @date: 2018-01-09
 */
#include <libblockverifier/ParaTxExecutor.h>
#include <libblockverifier/TxDAG.h>
#include <libdevcore/concurrent_queue.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <functional>
#include <thread>
#include <vector>

using namespace std;
using namespace dev;
using namespace dev::blockverifier;

namespace dev
{
namespace test
{
struct ParaTxExecutorTestFixture : public TestOutputHelperFixture
{
    ParaTxExecutorTestFixture() { threadNum = thread::hardware_concurrency(); }

    unsigned threadNum;
};

struct FakeTxDAG : TxDAG
{
    FakeTxDAG() : count(0)
    {
        for (int i = 0; i < taskNum; ++i)
        {
            taskQueue.push([this]() { count.fetch_add(1); });
        }
    }
    bool hasFinished() override
    {
        auto value = count.load();
        if (value < taskNum)
        {
            return false;
        }
        return true;
    }

    void executeUnit()
    {
        auto ret = taskQueue.tryPop(30);
        if (ret.first)
        {
            ret.second();
        }
    }

    atomic_int count;
    int taskNum = 100;
    concurrent_queue<function<void(void)>> taskQueue;
};

BOOST_FIXTURE_TEST_SUITE(ParaTxExecutorTest, ParaTxExecutorTestFixture)

BOOST_AUTO_TEST_CASE(testCountDownLatch)
{
    CountDownLatch latch(threadNum);
    BOOST_CHECK(static_cast<unsigned>(latch.getCount()) == threadNum);

    auto threadFunc = [](CountDownLatch& _latch) { _latch.countDown(); };

    vector<thread> threads;

    for (unsigned i = 0; i < threadNum; ++i)
    {
        threads.push_back(thread(threadFunc, std::ref(latch)));
    }

    for (unsigned i = 0; i < threadNum; ++i)
    {
        threads[i].join();
    }

    BOOST_CHECK(latch.getCount() == 0);
}

BOOST_AUTO_TEST_CASE(testWakeupNotifier)
{
    WakeupNotifier notifier;
    BOOST_CHECK(notifier.isReady() == false);
    atomic_int count(0);

    auto threadFunc = [&count](WakeupNotifier& _notifier) {
        _notifier.wait();
        count.fetch_add(1);
    };

    vector<thread> threads;
    for (unsigned i = 0; i < threadNum; ++i)
    {
        threads.push_back(thread(threadFunc, std::ref(notifier)));
    }
    BOOST_CHECK(count.load() == 0);

    notifier.notify();

    for (unsigned i = 0; i < threadNum; ++i)
    {
        threads[i].join();
    }
    BOOST_CHECK(static_cast<unsigned>(count.load()) == threadNum);

    BOOST_CHECK(notifier.isReady() == true);
    notifier.reset();
    BOOST_CHECK(notifier.isReady() == false);
}

BOOST_AUTO_TEST_CASE(testParaTxExecutor)
{
    ParaTxExecutor executor;
    executor.initialize();

    auto fakeDAG = std::make_shared<FakeTxDAG>();
    BOOST_CHECK(fakeDAG->count.load() == 0);
    executor.start(fakeDAG);
    BOOST_CHECK(fakeDAG->count.load() == fakeDAG->taskNum);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev