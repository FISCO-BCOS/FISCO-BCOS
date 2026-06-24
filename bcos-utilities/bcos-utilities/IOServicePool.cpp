/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/Common.h"
#include <range/v3/algorithm/sort.hpp>

using namespace bcos;

IOServicePool::IOServicePool(size_t _workerNum, std::string_view _threadName)
  : m_threadName(_threadName.substr(0, 15))
{
    m_contexts.reserve(_workerNum);
    for (size_t i = 0; i < _workerNum; i++)
    {
        m_contexts.emplace_back(std::make_shared<IOService>(1));
    }

    for (auto& ctx : m_contexts)
    {
        ctx.thread = std::thread([ioService = ctx.ioService, name = m_threadName]() {
            bcos::pthread_setThreadName(name);
            ioService->run();
        });
    }

    // Collect and sort thread IDs for binary search in dispatch()
    m_threadIds.reserve(m_contexts.size());
    for (auto& ctx : m_contexts)
    {
        m_threadIds.push_back(ctx.thread.get_id());
    }
    ::ranges::sort(m_threadIds);
}

IOServicePool::~IOServicePool()
{
    for (auto& ctx : m_contexts)
    {
        ctx.ioService->stop();
    }
    for (auto& ctx : m_contexts)
    {
        if (ctx.thread.joinable())
        {
            // Handle the edge case where the pool is destroyed on its own
            // worker thread (e.g. when FrontService destruction is triggered
            // by a temporary shared_ptr held by a completion handler).
            // Joining self would cause pthread_join EDEADLK.
            if (ctx.thread.get_id() == std::this_thread::get_id())
            {
                // The io_context has already been stopped, so run() will
                // return shortly.  Detach so the thread can exit cleanly.
                ctx.thread.detach();
            }
            else
            {
                ctx.thread.join();
            }
        }
    }
}

std::shared_ptr<IOServicePool::IOService>& IOServicePool::getIOService()
{
    auto idx = (m_nextIOService.fetch_add(1) % m_contexts.size());
    return m_contexts[idx].ioService;
}

bcos::IOServicePool::IOServiceContext::IOServiceContext(std::shared_ptr<IOService> _ioService)
  : ioService(std::move(_ioService)), work(this->ioService->get_executor())
{}

void Strand::Impl::kick()
{
    auto self = pool.lock();
    if (!self)
    {
        // Pool is gone — clear queue and bail out.
        std::lock_guard<std::mutex> lock(mutex);
        queue.clear();
        busy = false;
        return;
    }
    // Capture a shared_ptr to Impl so that the queue / mutex / busy stay
    // alive even if the owning Strand is destroyed before this handler runs.
    std::shared_ptr<Impl> impl = shared_from_this();
    self->post([impl]() { impl->drain(); });
}

void Strand::Impl::drain()
{
    std::function<void()> task;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty())
        {
            busy = false;
            return;
        }
        task = std::move(queue.front());
        queue.pop_front();
    }
    task();
    // Re-check queue AFTER task completes.  If a new task was enqueued while
    // we were executing task(), the queue is non-empty and we kick the next
    // drain.  Only set busy=false when the queue is truly empty — this
    // closes the race where busy was cleared before task() finished.
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty())
        {
            busy = false;
            return;
        }
    }
    kick();
}
