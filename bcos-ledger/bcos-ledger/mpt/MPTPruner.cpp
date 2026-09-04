/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file MPTPruner.cpp
 * @brief The pruner's private worker strand (the non-template half of MPTPruner)
 */
#include "MPTPruner.h"
#include <future>

namespace bcos::ledger::mpt::detail
{

AsyncWorker::AsyncWorker() : m_thread([this]() { run(); }) {}

AsyncWorker::~AsyncWorker()
{
    {
        std::lock_guard const lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_one();
    m_thread.join();
}

void AsyncWorker::post(std::function<void()> job) noexcept
{
    try
    {
        {
            std::lock_guard const lock(m_mutex);
            m_jobs.emplace_back(std::move(job));
        }
        m_cv.notify_one();
    }
    catch (...)
    {
        // Commit-path contract: never throw. The dropped job's work is idempotent and is
        // re-posted by the next onCommit (or by init() after a restart).
    }
}

void AsyncWorker::waitForIdle()
{
    std::promise<void> done;
    auto future = done.get_future();
    post([&done]() {
        try
        {
            done.set_value();
        }
        catch (...)
        {}
    });
    future.wait();
}

void AsyncWorker::run()
{
    while (true)
    {
        std::function<void()> job;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this]() { return m_stop || !m_jobs.empty(); });
            if (m_jobs.empty())
            {
                return;  // m_stop with a drained queue
            }
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        try
        {
            job();
        }
        catch (std::exception const& e)
        {
            BCOS_LOG(WARNING) << "MPTPruner worker job failed: "
                              << boost::diagnostic_information(e);
        }
        catch (...)
        {
            BCOS_LOG(WARNING) << "MPTPruner worker job failed with an unknown error";
        }
    }
}

}  // namespace bcos::ledger::mpt::detail
