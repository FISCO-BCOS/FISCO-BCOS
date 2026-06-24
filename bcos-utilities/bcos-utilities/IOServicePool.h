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
class IOServicePool;

// Standalone serial task dispatcher that wraps an IOServicePool.
// Tasks submitted via post() are guaranteed to execute in FIFO order
// and never concurrently, while still being round-robin dispatched
// across the pool's io_contexts.  Multiple Strand instances can coexist
// independently — each serializes its own tasks without blocking others.
//
// Lifetime: the Strand can be safely destroyed even when drain handlers
// are still queued in the pool — the inner Impl struct is kept alive by
// the posted handlers until the queue is fully drained.
class Strand
{
    struct Impl : public std::enable_shared_from_this<Impl>
    {
        std::weak_ptr<IOServicePool> pool;
        std::deque<std::function<void()>> queue;
        std::mutex mutex;
        bool busy{false};

        void kick();
        void drain();
    };

public:
    using Ptr = std::shared_ptr<Strand>;

    explicit Strand(const std::shared_ptr<IOServicePool>& pool)
    {
        m_impl = std::make_shared<Impl>();
        m_impl->pool = pool;
    }

    ~Strand()
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->queue.clear();
        m_impl->busy = false;
    }

    Strand(const Strand&) = delete;
    Strand& operator=(const Strand&) = delete;
    Strand(Strand&&) = delete;
    Strand& operator=(Strand&&) = delete;

    template <class Task>
    void post(Task&& task)
    {
        bool needKick = false;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            m_impl->queue.emplace_back(std::forward<Task>(task));
            needKick = !m_impl->busy;
            if (needKick)
            {
                m_impl->busy = true;
            }
        }
        if (needKick)
        {
            m_impl->kick();
        }
    }

private:
    std::shared_ptr<Impl> m_impl;
};

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

private:
    struct IOServiceContext
    {
        std::shared_ptr<IOService> ioService;
        Work work;
        std::thread thread;

        explicit IOServiceContext(std::shared_ptr<IOService> _ioService);
    };

    std::vector<IOServiceContext> m_contexts;
    std::vector<std::thread::id> m_threadIds;
    std::string m_threadName;
    std::atomic_size_t m_nextIOService = 0;
};
}  // namespace bcos
