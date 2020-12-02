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
 * @file Worker.cpp
 */
#include "Worker.h"
#include <pthread.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <chrono>
#include <thread>
using namespace std;
using namespace bcos;

void setThreadName(const char* _threadName)
{
#if defined(__GLIBC__)
    pthread_setname_np(pthread_self(), _threadName);
#elif defined(__APPLE__)
    pthread_setname_np(_threadName);
#endif
}

void Worker::startWorking()
{
    boost::unique_lock<boost::mutex> l(x_work);
    if (m_workerThread)
    {
        WorkerState workerState = WorkerState::Stopped;
        m_workerState.compare_exchange_strong(workerState, WorkerState::Starting);
        m_workerStateNotifier.notify_all();
    }
    else
    {
        m_workerState = WorkerState::Starting;
        m_workerStateNotifier.notify_all();
        m_workerThread.reset(new thread([&]() {
            // set threadName for log
            if (boost::log::core::get())
            {
                std::vector<std::string> fields;
                boost::split(fields, m_threadName, boost::is_any_of("-"), boost::token_compress_on);
                if (fields.size() > 1)
                {
                    boost::log::core::get()->add_thread_attribute(
                        "GroupId", boost::log::attributes::constant<std::string>(fields[1]));
                }
            }
            setThreadName(m_threadName.c_str());
            while (m_workerState != WorkerState::Killing)
            {
                WorkerState ex = WorkerState::Starting;
                {
                    // the condition variable-related lock
                    boost::unique_lock<boost::mutex> l(x_work);
                    m_workerState = WorkerState::Started;
                }

                m_workerStateNotifier.notify_all();

                try
                {
                    startedWorking();
                    taskProcessLoop();
                    doneWorking();
                }
                catch (std::exception const& e)
                {
                    LOG(ERROR) << LOG_DESC("Exception thrown in Worker thread")
                               << LOG_KV("threadName", m_threadName)
                               << LOG_KV("errorMsg", boost::diagnostic_information(e));
                }

                {
                    // the condition variable-related lock
                    boost::unique_lock<boost::mutex> l(x_work);
                    ex = m_workerState.exchange(WorkerState::Stopped);
                    if (ex == WorkerState::Killing || ex == WorkerState::Starting)
                        m_workerState.exchange(ex);
                }
                m_workerStateNotifier.notify_all();

                {
                    boost::unique_lock<boost::mutex> l(x_work);
                    TIME_RECORD("Worker stopping");
                    while (m_workerState == WorkerState::Stopped)
                        m_workerStateNotifier.wait_for(l, boost::chrono::milliseconds(100));
                }
            }
        }));
    }

    TIME_RECORD("Start worker");
    while (m_workerState == WorkerState::Starting)
        m_workerStateNotifier.wait_for(l, boost::chrono::milliseconds(100));
}

void Worker::stopWorking()
{
    boost::unique_lock<boost::mutex> l(x_work);
    if (m_workerThread)
    {
        WorkerState ex = WorkerState::Started;
        if (!m_workerState.compare_exchange_strong(ex, WorkerState::Stopping))
            return;
        m_workerStateNotifier.notify_all();
        TIME_RECORD("Stop worker");
        while (m_workerState != WorkerState::Stopped)
        {
            m_workerStateNotifier.wait_for(l, boost::chrono::milliseconds(100));
        }
    }
}

void Worker::terminate()
{
    boost::unique_lock<boost::mutex> l(x_work);
    if (m_workerThread)
    {
        if (m_workerState.exchange(WorkerState::Killing) == WorkerState::Killing)
            return;  // Somebody else is doing this
        l.unlock();
        m_workerStateNotifier.notify_all();
        TIME_RECORD("Terminate worker");
        m_workerThread->join();

        l.lock();
        m_workerThread.reset();
    }
}

void Worker::taskProcessLoop()
{
    while (m_workerState == WorkerState::Started)
    {
        if (m_idleWaitMs)
            this_thread::sleep_for(chrono::milliseconds(m_idleWaitMs));
        executeTask();
    }
}
