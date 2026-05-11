/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "bcos-utilities/ThreadPool.h"

using namespace bcos;

ThreadPool::ThreadPool(std::string threadName, size_t size, bool dispatch)
  : m_threadName(std::move(threadName)), m_workGuard(boost::asio::make_work_guard(m_ioService))
{
    std::ignore = dispatch;
    for (size_t i = 0; i < size; ++i)
    {
        m_workers.create_thread([this] {
            bcos::pthread_setThreadName(m_threadName);
            m_ioService.run();
        });
    }
}

void ThreadPool::stop()
{
    m_ioService.stop();
    if (!m_workers.is_this_thread_in())
    {
        m_workers.join_all();
    }
}

ThreadPool::~ThreadPool()
{
    stop();
}

bool ThreadPool::hasStopped()
{
    return m_ioService.stopped();
}
