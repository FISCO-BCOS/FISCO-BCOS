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
 *  limitations under the License.
 *
 * @file Worker.h
 */

#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <utility>

namespace bcos
{
enum class WorkerState
{
    Stopped,
    Started
};

class Worker : public std::enable_shared_from_this<Worker>
{
protected:
    Worker(boost::asio::io_context& _ioContext, std::string _threadName = "worker",
        unsigned _idleWaitMs = 30)
      : m_ioContext(_ioContext),
        m_timer(_ioContext),
        m_threadName(std::move(_threadName)),
        m_idleWaitMs(_idleWaitMs)
    {}
    virtual ~Worker();

    /**
     * @brief Set thread name for the worker
     * @param _threadName: the thread name
     */
    void setName(std::string const& _threadName)
    {
        // m_threadName is read by the io_context thread in every log line
        // (LOG_KV("threadName", m_threadName)).  Only allow writes while
        // the worker is not running to avoid a data race on std::string.
        if (!isWorking())
            m_threadName = _threadName;
    }

    std::string const& threadName() const { return m_threadName; }

    // Starts worker by scheduling the first timer tick
    void startWorking();

    // Stop worker by cancelling the timer
    void stopWorking();

    // Return true if the worker is actively running
    bool isWorking() const { return m_workerState == WorkerState::Started; }

    // Called after startWorking to init the worker (one-shot)
    virtual void initWorker() {}

    // worker execute logic (Called per timer tick)
    virtual void executeWorker() {}

    bool shouldStop() const { return m_workerState != WorkerState::Started; }

    // Called when is to be stopped
    virtual void finishWorker() {}

    // Wake up the worker immediately (cancel current timer, re-schedule)
    void notify();

    std::atomic<WorkerState>& workerState() { return m_workerState; }
    unsigned idleWaitMs() const { return m_idleWaitMs; }

private:
    // Schedule the next timer tick
    void scheduleNext();
    // Timer completion handler (body of the former doTick lambda)
    void handleTimerTick(boost::system::error_code const& _ec);

    boost::asio::io_context& m_ioContext;
    boost::asio::steady_timer m_timer;

    std::string m_threadName;

    unsigned m_idleWaitMs = 0;

    std::atomic<WorkerState> m_workerState = {WorkerState::Stopped};
};

}  // namespace bcos
