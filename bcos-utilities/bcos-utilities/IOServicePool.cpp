/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/Common.h"

using namespace bcos;

IOServicePool::IOServicePool(size_t _workerNum) : m_works(_workerNum), m_nextIOService(0)
{
    for (size_t i = 0; i < _workerNum; i++)
    {
        m_ioServices.emplace_back(std::make_shared<IOService>());
    }
}

IOServicePool::~IOServicePool()
{
    stop();
}

void IOServicePool::start()
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

    for (const auto& ioService : m_ioServices)
    {
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

std::shared_ptr<IOServicePool::IOService>& IOServicePool::getIOService()
{
    auto selectedIoService = (m_nextIOService.fetch_add(1) % m_ioServices.size());
    return m_ioServices.at(selectedIoService);
}

void IOServicePool::stop()
{
    if (!m_running)
    {
        return;
    }
    m_running = false;

    // 1. Reset work to release keep-alive, allowing io_context to exit when no handlers remain
    for (auto& work : m_works)
    {
        work.reset();
    }

    // 2. Signal all io_contexts to stop. This causes run() to return after finishing
    //    currently queued handlers. Without this, recurring timers/callbacks would
    //    keep run() alive indefinitely, causing join() to hang.
    for (auto& ioService : m_ioServices)
    {
        ioService->stop();
    }

    // 3. Wait for all threads to exit
    for (auto& thread : m_threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    m_threads.clear();
}
