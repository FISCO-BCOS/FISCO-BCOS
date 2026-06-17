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
    WorkerState expected = WorkerState::Stopped;
    if (!m_workerState.compare_exchange_strong(expected, WorkerState::Started))
    {
        return;  // Already running, or lost the race
    }

    // Reset the alive flag so that async handlers from a previous stop
    // cycle don't prematurely bail out of a restarted worker.
    *m_aliveFlag = true;

    // initWorker() is safe to call synchronously — subclasses only log.
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

    // scheduleNext() mutates m_timer (expires_after / async_wait), which
    // must only happen on the io_context thread.  Post it there so that
    // callers from arbitrary threads (e.g. Sealer::start()) are safe.
    boost::asio::post(m_ioContext, [this]() {
        if (m_workerState == WorkerState::Started)
        {
            scheduleNext();
        }
    });
}

void Worker::stopWorking()
{
    WorkerState expected = WorkerState::Started;
    if (!m_workerState.compare_exchange_strong(expected, WorkerState::Stopped))
    {
        return;  // Not running, or another thread is already stopping
    }

    // Signal all in-flight handlers to bail out before accessing `this`.
    // The CAS above already changed m_workerState to Stopped, and
    // m_aliveFlag=false makes every captured handler return immediately.
    *m_aliveFlag = false;

    // Route cancel through the io_context thread so that it doesn't race
    // with handleTimerTick / scheduleNext executing on the io_context.
    boost::asio::post(m_ioContext, [this]() { m_timer.cancel(); });

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
        // Post the re-schedule to the io_context thread because
        // scheduleNext() mutates m_timer (expires_after / async_wait).
        // The explicit cancel is not needed — expires_after() inside
        // scheduleNext() implicitly cancels any pending async_wait.
        boost::asio::post(m_ioContext, [this]() {
            if (m_workerState == WorkerState::Started)
            {
                scheduleNext();
            }
        });
    }
}
