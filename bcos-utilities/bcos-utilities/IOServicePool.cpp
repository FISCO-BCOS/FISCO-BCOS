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
        // Pool is gone — clear queue and reset count.
        std::lock_guard<std::mutex> lock(mutex);
        queue.clear();
        count_.store(0, std::memory_order_release);
        return;
    }
    // Capture a shared_ptr to Impl so that the queue / mutex / count_ stay
    // alive even if the owning Strand is destroyed before this handler runs.
    std::shared_ptr<Impl> impl = shared_from_this();
    self->post([impl]() { impl->drain(); });
}

void Strand::Impl::drain()
{
    std::function<void()> task;
    {
        std::lock_guard<std::mutex> lock(mutex);
        // Defensive: queue could be empty if kick() was called after
        // the pool expired and cleared everything.  ~Strand() no longer
        // clears the queue, so under normal operation this guard never
        // triggers; it only protects against pool-lifetime edge cases.
        if (queue.empty())
        {
            return;
        }
        task = std::move(queue.front());
        queue.pop_front();
    }
    IOServicePool::safeExecute(std::move(task));

    // Atomically release our "running" slot.  fetch_sub returns the value
    // *before* subtraction.
    if (auto prev = count_.fetch_sub(1, std::memory_order_acq_rel); prev == 1)
    {
        // Only our running slot existed — no new tasks were posted during
        // execution.  Verify under lock to close a narrow race: a post()
        // between our fetch_sub and the lock below.
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty())
        {
            return;  // Truly done.
        }
        // A post() snuck in and already called kick() for us (it saw
        // count_ go from 0 to 1).  Restore the running count so the
        // already-posted drain can proceed, and return without kicking.
        count_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // prev > 1: more tasks were definitely enqueued during our execution.
    // No lock needed here — just kick the next drain.
    kick();
}
