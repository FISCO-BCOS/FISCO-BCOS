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
#include <boost/asio.hpp>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <range/v3/algorithm/binary_search.hpp>
#include <string>
#include <string_view>
#include <thread>
namespace bcos
{
class IOServicePool
{
public:
    using Ptr = std::shared_ptr<IOServicePool>;

    using IOService = boost::asio::io_context;
    using ExecutorType = boost::asio::io_context::executor_type;
    using Work = boost::asio::executor_work_guard<ExecutorType>;
    explicit IOServicePool(size_t _workerNum = std::thread::hardware_concurrency() + 1,
        std::string_view _threadName = "ioService");

    IOServicePool(const IOServicePool&) = delete;
    IOServicePool& operator=(const IOServicePool&) = delete;
    IOServicePool(IOServicePool&&) = delete;
    IOServicePool& operator=(IOServicePool&&) = delete;
    ~IOServicePool();

    std::shared_ptr<IOService>& getIOService();

    template <class Task>
    void post(Task&& task)
    {
        auto& ioService = getIOService();
        boost::asio::post(ioService->get_executor(), std::forward<Task>(task));
    }

    template <class Task>
    void dispatch(Task&& task)
    {
        auto id = std::this_thread::get_id();
        if (::ranges::binary_search(m_threadIds, id))
        {
            task();
        }
        else
        {
            post(std::forward<Task>(task));
        }
    }

    // Submit a task that is guaranteed to execute serially with respect to
    // other tasks submitted via strand() (FIFO order).  Unlike
    // boost::asio::strand which binds to a single executor, this
    // implementation round-robins each task across the pool's io_contexts
    // so that strand work is spread over all threads while never executing
    // two strand tasks concurrently.
    template <class Task>
    void strand(Task&& task)
    {
        bool needKick = false;
        {
            std::lock_guard<std::mutex> lock(m_strandMutex);
            m_strandQueue.emplace_back(std::forward<Task>(task));
            needKick = !m_strandBusy;
            if (needKick)
            {
                m_strandBusy = true;
            }
        }
        if (needKick)
        {
            kickStrand();
        }
    }

private:
    struct IOServiceContext
    {
        std::shared_ptr<IOService> ioService;
        Work work;
        std::thread thread;

        explicit IOServiceContext(std::shared_ptr<IOService> _ioService);
    };
    void kickStrand();
    void drainStrand();

    std::vector<IOServiceContext> m_contexts;
    std::vector<std::thread::id> m_threadIds;
    std::string m_threadName;
    std::atomic_size_t m_nextIOService = 0;

    // Custom strand: deque-based serial task queue with round-robin dispatch
    // across all pool io_contexts.  Only one task is in-flight at a time.
    std::deque<std::function<void()>> m_strandQueue;
    std::mutex m_strandMutex;
    bool m_strandBusy{false};
};
}  // namespace bcos
