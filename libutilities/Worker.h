/*
 *  Copyright (C) 2020 FISCO BCOS.
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

#include "Common.h"
#include <atomic>
#include <string>
#include <thread>

namespace bcos
{
enum class WorkerState
{
    Starting,
    Started,
    Stopping,
    Stopped,
    Killing
};

class Worker
{
protected:
    Worker(std::string const& _threadName = "worker", unsigned _idleWaitMs = 30)
      : m_threadName(_threadName), m_idleWaitMs(_idleWaitMs)
    {}
    virtual ~Worker() { terminate(); }

    /**
     * @brief Set thread name for the worker
     *
     * @param _threadName: the thread name
     */
    void setName(std::string const& _threadName)
    {
        if (!isWorking())
            m_threadName = _threadName;
    }

    std::string const& threadName() const { return m_threadName; }

    /// Starts worker thread by calling startedWorking
    void startWorking();

    /// Stop worker thread
    void stopWorking();

    // Return true if the worker thread is working
    bool isWorking() const
    {
        boost::unique_lock<boost::mutex> l(x_work);
        return m_workerState == WorkerState::Started;
    }

    // Called after thread is started from startWorking().
    virtual void startedWorking() {}

    // task execute logic (Called continuously following sleep for m_idleWaitMs)
    virtual void executeTask() {}

    // schedule the task inner a loop before stop the worker thread
    virtual void taskProcessLoop();
    bool shouldStop() const { return m_workerState != WorkerState::Started; }

    /// Called when is to be stopped, just prior to thread being joined.
    virtual void doneWorking() {}
    // stop the worker
    void terminate();

    std::atomic<WorkerState>& workerState() { return m_workerState; }
    unsigned idleWaitMs() { return m_idleWaitMs; }

private:
    std::string m_threadName;

    unsigned m_idleWaitMs = 0;

    mutable boost::mutex x_work;
    // the worker thread
    std::unique_ptr<std::thread> m_workerThread;
    // Notification when m_workerState changes
    mutable boost::condition_variable m_workerStateNotifier;
    std::atomic<WorkerState> m_workerState = {WorkerState::Starting};
};

}  // namespace bcos
