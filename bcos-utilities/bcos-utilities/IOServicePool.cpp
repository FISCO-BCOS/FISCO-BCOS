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
            ctx.thread.join();
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
