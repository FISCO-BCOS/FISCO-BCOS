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
    if (m_work)
    {
        WorkerState ex = WorkerState::Stopped;
        m_state.compare_exchange_strong(ex, WorkerState::Starting);
        m_stateNotifier.notify_all();
    }
    else
    {
        m_state = WorkerState::Starting;
        m_stateNotifier.notify_all();
        m_work.reset(new thread([&]() {
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
            while (m_state != WorkerState::Killing)
            {
                WorkerState ex = WorkerState::Starting;
                {
                    // the condition variable-related lock
                    boost::unique_lock<boost::mutex> l(x_work);
                    m_state = WorkerState::Started;
                }

                m_stateNotifier.notify_all();

                try
                {
                    startedWorking();
                    workLoop();
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
                    ex = m_state.exchange(WorkerState::Stopped);
                    if (ex == WorkerState::Killing || ex == WorkerState::Starting)
                        m_state.exchange(ex);
                }
                m_stateNotifier.notify_all();

                {
                    boost::unique_lock<boost::mutex> l(x_work);
                    TIME_RECORD("Worker stopping");
                    while (m_state == WorkerState::Stopped)
                        m_stateNotifier.wait_for(l, boost::chrono::milliseconds(100));
                }
            }
        }));
    }

    TIME_RECORD("Start worker");
    while (m_state == WorkerState::Starting)
        m_stateNotifier.wait_for(l, boost::chrono::milliseconds(100));
}

void Worker::stopWorking()
{
    boost::unique_lock<boost::mutex> l(x_work);
    if (m_work)
    {
        WorkerState ex = WorkerState::Started;
        if (!m_state.compare_exchange_strong(ex, WorkerState::Stopping))
            return;
        m_stateNotifier.notify_all();
        TIME_RECORD("Stop worker");
        while (m_state != WorkerState::Stopped)
        {
            m_stateNotifier.wait_for(l, boost::chrono::milliseconds(100));
        }
    }
}

void Worker::terminate()
{
    boost::unique_lock<boost::mutex> l(x_work);
    if (m_work)
    {
        if (m_state.exchange(WorkerState::Killing) == WorkerState::Killing)
            return;  // Somebody else is doing this
        l.unlock();
        m_stateNotifier.notify_all();
        TIME_RECORD("Terminate worker");
        m_work->join();

        l.lock();
        m_work.reset();
    }
}

void Worker::workLoop()
{
    while (m_state == WorkerState::Started)
    {
        if (m_idleWaitMs)
            this_thread::sleep_for(chrono::milliseconds(m_idleWaitMs));
        doWork();
    }
}
