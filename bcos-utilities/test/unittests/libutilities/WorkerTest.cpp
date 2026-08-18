/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief Construct a new boost auto test case object for Worker
 *
 * @file Worker.cpp
 * @author: tabsu
 */

#include "bcos-utilities/Worker.h"
#include "bcos-utilities/Timer.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <chrono>
#include <boost/test/unit_test.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <thread>
using namespace bcos;
using namespace std;

namespace bcos
{
namespace test
{
class TestWorkerImpl : public Worker
{
public:
    TestWorkerImpl(boost::asio::io_context& io) : Worker(io, "TestWorkerImpl", 1)
    {
        m_timer = std::make_shared<Timer>(io, 1, "testTimer");
        m_timer->registerTimeoutHandler([]() { std::cout << "#### call timer" << std::endl; });
    }
    void run() { startWorking(); }
    void stop() { stopWorking(); }

protected:
    virtual void initWorker() { cout << "initWorker..." << endl; }
    virtual void executeWorker()
    {
        cout << "count:" << count << endl;
        count++;
    }
    virtual void finishWorker() { cout << "finishWorker..." << endl; }

private:
    int count = 0;
    std::shared_ptr<Timer> m_timer;
};

BOOST_FIXTURE_TEST_SUITE(Worker, TestPromptFixture)

BOOST_AUTO_TEST_CASE(testWorker)
{
    boost::asio::io_context ioContext;
    auto work = boost::asio::make_work_guard(ioContext);
    std::thread ioThread([&]() { ioContext.run(); });

    TestWorkerImpl workerImpl(ioContext);
    workerImpl.run();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    workerImpl.stop();

    work.reset();
    ioContext.stop();
    ioThread.join();
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
