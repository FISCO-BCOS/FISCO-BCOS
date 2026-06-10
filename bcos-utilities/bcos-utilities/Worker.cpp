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
 * @file Worker.cpp
 */
#include "Worker.h"

#include "BoostLog.h"
#include <boost/asio/post.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <exception>
#include <functional>

using namespace bcos;

void Worker::startWorking()
{
    boost::unique_lock<boost::mutex> l(x_work);
    if (m_workerState == WorkerState::Started)
    {
        // Already running
        return;
    }
    // Transition to Starting, then schedule the first tick
    m_workerState = WorkerState::Starting;

    // Capture a weak reference to this in case the Worker is destroyed
    // before the timer fires. Use a raw pointer with a shareable flag.
    boost::asio::post(m_ioContext, [this]() {
        if (m_workerState != WorkerState::Starting)
        {
            return;  // was stopped before the post ran
        }
        try
        {
            initWorker();
        }
        catch (std::exception const& e)
        {
            BCOS_LOG(WARNING) << LOG_DESC("Exception in Worker initWorker")
                              << LOG_KV("threadName", m_threadName)
                              << LOG_KV("msg", boost::diagnostic_information(e));
        }

        m_workerState = WorkerState::Started;

        // Schedule the first tick
        scheduleNext();
    });
}

void Worker::stopWorking()
{
    boost::unique_lock<boost::mutex> l(x_work);
    if (m_workerState != WorkerState::Started)
    {
        return;
    }
    m_workerState = WorkerState::Stopping;
    // Cancel the timer to stop the loop
    m_timer.cancel();
    try
    {
        finishWorker();
    }
    catch (std::exception const& e)
    {
        BCOS_LOG(WARNING) << LOG_DESC("Exception in Worker finishWorker")
                          << LOG_KV("threadName", m_threadName)
                          << LOG_KV("msg", boost::diagnostic_information(e));
    }
    m_workerState = WorkerState::Stopped;
}

void Worker::terminate()
{
    boost::unique_lock<boost::mutex> l(x_work);
    if (m_workerState == WorkerState::Killing)
    {
        return;  // Already terminating
    }
    m_workerState = WorkerState::Killing;
    m_timer.cancel();
}

void Worker::workerProcessLoop()
{
    // Default: just call executeWorker once per tick
    executeWorker();
}

void Worker::scheduleNext()
{
    if (m_workerState != WorkerState::Started)
    {
        return;
    }

    auto self = this;
    std::function<void(boost::system::error_code const&)> doTick;
    doTick = [self, &doTick](boost::system::error_code const& ec) {
            if (ec == boost::asio::error::operation_aborted)
            {
                return;  // Timer was cancelled
            }
            if (self->m_workerState != WorkerState::Started)
            {
                return;
            }
            bool hasException = false;
            try
            {
                self->workerProcessLoop();
            }
            catch (std::exception const& e)
            {
                hasException = true;
                BCOS_LOG(WARNING) << LOG_DESC("Exception in Worker workerProcessLoop")
                                  << LOG_KV("threadName", self->m_threadName)
                                  << LOG_KV("msg", boost::diagnostic_information(e));
            }
            // FIB-111: backoff after exceptions to prevent tight CPU spin
            if (hasException && self->m_idleWaitMs == 0)
            {
                // Use a small backoff to avoid busy-looping on persistent exceptions
                self->m_timer.expires_after(boost::asio::chrono::milliseconds(10));
                self->m_timer.async_wait(doTick);
            }
            else
            {
                self->scheduleNext();
            }
        };

    if (m_idleWaitMs > 0)
    {
        m_timer.expires_after(boost::asio::chrono::milliseconds(m_idleWaitMs));
    }
    else
    {
        m_timer.expires_after(boost::asio::chrono::milliseconds(0));
    }
    m_timer.async_wait(doTick);
}

void Worker::notify()
{
    if (m_workerState == WorkerState::Started)
    {
        m_timer.cancel();
        // scheduleNext will be called from the cancelled timer's handler
        // (with ec == operation_aborted), so we need to reschedule here
        boost::asio::post(m_ioContext, [this]() {
            if (m_workerState == WorkerState::Started)
            {
                scheduleNext();
            }
        });
    }
}
