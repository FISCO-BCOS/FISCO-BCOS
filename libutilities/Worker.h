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
enum class IfRunning
{
    Fail,
    Join,
    Detach
};

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

    /// Move-constructor.
    Worker(Worker&& _m) { std::swap(m_threadName, _m.m_threadName); }

    /// Move-assignment.
    Worker& operator=(Worker&& _m)
    {
        assert(&_m != this);
        std::swap(m_threadName, _m.m_threadName);
        return *this;
    }

    virtual ~Worker() { terminate(); }

    /// Allows changing worker name if work is stopped.
    /// m_threadName: ${module}-${groupID}
    void setName(std::string const& _n)
    {
        if (!isWorking())
            m_threadName = _n;
    }

    std::string const& name() const { return m_threadName; }
    /// Starts worker thread; causes startedWorking() to be called.
    void startWorking();

    /// Stop worker thread; causes call to stopWorking().
    void stopWorking();

    /// Returns if worker thread is present.
    bool isWorking() const
    {
        boost::unique_lock<boost::mutex> l(x_work);
        return m_state == WorkerState::Started;
    }

    /// Called after thread is started from startWorking().
    virtual void startedWorking() {}

    /// Called continuously following sleep for m_idleWaitMs.
    virtual void doWork() {}

    /// Overrides doWork(); should call shouldStop() often and exit when true.
    virtual void workLoop();
    bool shouldStop() const { return m_state != WorkerState::Started; }

    /// Called when is to be stopped, just prior to thread being joined.
    virtual void doneWorking() {}

    /// Stop and never start again.
    /// This has to be called in the destructor of any most derived class.  Otherwise the worker
    /// thread will try to lookup vptrs. It's OK to call terminate() in destructors of multiple
    /// derived classes.
    void terminate();

    std::atomic<WorkerState>& workerState() { return m_state; }
    unsigned idleWaitMs() { return m_idleWaitMs; }

private:
    std::string m_threadName;

    unsigned m_idleWaitMs = 0;

    mutable boost::mutex x_work;          ///< Lock for the network existence and m_stateNotifier.
    std::unique_ptr<std::thread> m_work;  ///< The network thread.
    mutable boost::condition_variable m_stateNotifier;  //< Notification when m_state changes.
    std::atomic<WorkerState> m_state = {WorkerState::Starting};
};

}  // namespace bcos
