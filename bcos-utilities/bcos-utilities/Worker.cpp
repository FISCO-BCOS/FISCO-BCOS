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

using namespace bcos;

void Worker::startWorking()
{
    std::unique_lock l(x_work);
    if (m_workerState == WorkerState::Started)
    {
        // Already running
        return;
    }
    // Set Started synchronously so that isWorking() immediately reflects
    // the intent. The actual timer scheduling still happens asynchronously
    // via post(), but callers of stop() / terminate() / notify() can rely
    // on the state being consistent without racing the io_context thread.
    m_workerState = WorkerState::Started;

    auto aliveFlag = m_aliveFlag;
    boost::asio::post(m_ioContext, [this, aliveFlag]() {
        if (!*aliveFlag)
        {
            return;  // Worker was destroyed before the post ran
        }
        if (m_workerState != WorkerState::Started)
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

        // Schedule the first tick
        scheduleNext();
    });
}

void Worker::stopWorking()
{
    {
        std::unique_lock l(x_work);
        if (m_workerState != WorkerState::Started)
        {
            return;
        }
        m_workerState = WorkerState::Stopping;
        // Cancel the timer — cancel() returns immediately and is non-throwing.
        m_timer.cancel();
    }
    // Call finishWorker() outside the lock: subclasses may acquire other
    // locks or call back into Worker APIs, which could deadlock if x_work
    // were still held.
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
    std::unique_lock l(x_work);
    if (m_workerState == WorkerState::Killing)
    {
        return;  // Already terminating
    }
    m_workerState = WorkerState::Killing;

    // Signal all in-flight handlers to bail out before accessing `this`.
    // The shared_ptr guarantees the atomic<bool> outlives the Worker even
    // if a handler was already posted to the io_context queue.
    *m_aliveFlag = false;

    // Cancel any pending timer operation. cancel() is non-throwing and
    // thread-safe. Handlers already queued will see !*m_aliveFlag and bail.
    m_timer.cancel();
}

void Worker::scheduleNext()
{
    if (m_workerState != WorkerState::Started)
    {
        return;
    }

    m_timer.expires_after(boost::asio::chrono::milliseconds(m_idleWaitMs));
    auto aliveFlag = m_aliveFlag;
    m_timer.async_wait([this, aliveFlag](boost::system::error_code const& ec) {
        if (!*aliveFlag)
        {
            return;
        }
        handleTimerTick(ec);
    });
}

void Worker::handleTimerTick(boost::system::error_code const& ec)
{
    // aliveFlag check is performed by the wrapping lambda in scheduleNext()
    // and the backoff path below; kept here for defense-in-depth.
    if (!*m_aliveFlag)
    {
        return;
    }
    if (ec == boost::asio::error::operation_aborted)
    {
        return;  // Timer was cancelled
    }
    if (m_workerState != WorkerState::Started)
    {
        return;
    }
    bool hasException = false;
    try
    {
        executeWorker();
    }
    catch (std::exception const& e)
    {
        hasException = true;
        BCOS_LOG(WARNING) << LOG_DESC("Exception in Worker executeWorker")
                          << LOG_KV("threadName", m_threadName)
                          << LOG_KV("msg", boost::diagnostic_information(e));
    }
    // FIB-111: backoff after exceptions to prevent tight CPU spin
    if (hasException && m_idleWaitMs == 0)
    {
        // Use a small backoff to avoid busy-looping on persistent exceptions
        m_timer.expires_after(boost::asio::chrono::milliseconds(10));
        auto aliveFlag = m_aliveFlag;
        m_timer.async_wait([this, aliveFlag](boost::system::error_code const& ec) {
            if (!*aliveFlag)
            {
                return;
            }
            handleTimerTick(ec);
        });
    }
    else
    {
        scheduleNext();
    }
}

void Worker::notify()
{
    if (m_workerState == WorkerState::Started)
    {
        m_timer.cancel();
        // The cancelled timer's handler will receive operation_aborted and
        // return without rescheduling, so we explicitly post a fresh
        // scheduleNext() to wake the worker.
        auto aliveFlag = m_aliveFlag;
        boost::asio::post(m_ioContext, [this, aliveFlag]() {
            if (!*aliveFlag)
            {
                return;
            }
            if (m_workerState == WorkerState::Started)
            {
                scheduleNext();
            }
        });
    }
}
