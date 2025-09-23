/*
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
 *  m_limitations under the License.
 *
 * @file IOServicePool.h
 * @date 2022-06-14
 */

#pragma once
#include "bcos-utilities/Common.h"
#include <boost/asio.hpp>
#include <memory>
namespace bcos
{
class IOServicePool
{
public:
    using Ptr = std::shared_ptr<IOServicePool>;

    using IOService = boost::asio::io_context;
    using ExecutorType = boost::asio::io_context::executor_type;
    using Work = boost::asio::executor_work_guard<ExecutorType>;
    using WorkPtr = std::unique_ptr<Work>;
    explicit IOServicePool(size_t _workerNum = std::thread::hardware_concurrency())
      : m_works(_workerNum), m_nextIOService(0)
    {
        // create the ioservices
        for (size_t i = 0; i < _workerNum; i++)
        {
            m_ioServices.emplace_back(std::make_shared<IOService>());
        }
    }

    IOServicePool(const IOServicePool&) = delete;
    IOServicePool& operator=(const IOServicePool&) = delete;
    virtual ~IOServicePool() { stop(); }

    void start()
    {
        if (m_running)
        {
            return;
        }
        m_running = true;
        for (size_t i = 0; i < m_ioServices.size(); ++i)
        {
            m_works[i] = std::make_unique<Work>(m_ioServices[i]->get_executor());
        }

        // one io_context per thread
        for (const auto& ioService : m_ioServices)
        {
            // https://github.com/chriskohlhoff/asio/issues/932#issuecomment-968103444
            m_threads.emplace_back([ioService, running = std::ref(m_running)]() {
                if (!running)
                {
                    return;
                }

                bcos::pthread_setThreadName("ioService");
                ioService->run();
            });
        }
    }

    std::shared_ptr<IOService> getIOService()
    {
        auto selectedIoService = (m_nextIOService.fetch_add(1) % m_ioServices.size());
        return m_ioServices.at(selectedIoService);
    }

    void stop()
    {
        if (!m_running)
        {
            return;
        }
        m_running = false;

        // stop the work
        for (auto& work : m_works)
        {
            work.reset();
        }

        // stop the io service
        for (auto& ioService : m_ioServices)
        {
            ioService->stop();
        }

        // stop the thread
        for (auto& thread : m_threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }

        m_threads.clear();
    }

private:
    std::vector<std::shared_ptr<IOService>> m_ioServices;
    std::vector<WorkPtr> m_works;
    std::vector<std::thread> m_threads;
    std::atomic_size_t m_nextIOService = 0;
    bool m_running = false;
};
}  // namespace bcos
