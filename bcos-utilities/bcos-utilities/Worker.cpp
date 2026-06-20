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
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <exception>
#include <future>

using namespace bcos;

Worker::~Worker()
{
    stopWorking();

    // Drain barrier: flush all pending [this] handlers that were posted to
    // the io_context before the object is destroyed.  We post a no-op lambda
    // (no `this` capture) and wait for it to run; once it executes all
    // previously-queued handlers that captured raw `this` have been drained.
    //
    // Skip the barrier when the io_context has already been stopped (no
    // thread is processing its queue) or when the destructor is called from
    // the io_context thread itself (would deadlock).
    auto executor = m_ioContext.get_executor();
    if (!m_ioContext.stopped() && !executor.running_in_this_thread())
    {
        std::promise<void> drainPromise;
        auto drainFuture = drainPromise.get_future();
        boost::asio::post(m_ioContext, [&drainPromise]() { drainPromise.set_value(); });
        drainFuture.wait_for(std::chrono::seconds(60));
    }
}

void Worker::startWorking()
{
    WorkerState expected = WorkerState::Stopped;
    if (!m_workerState.compare_exchange_strong(expected, WorkerState::Started))
    {
        return;  // Already running, or lost the race
    }

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
    catch (...)
    {
        BCOS_LOG(WARNING) << LOG_DESC("Unknown exception in Worker initWorker")
                          << LOG_KV("threadName", m_threadName)
                          << LOG_KV("msg", boost::current_exception_diagnostic_information());
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
    // The CAS above already changed m_workerState to Stopped and the drain
    // barrier in ~Worker() ensures all [this] handlers are flushed.

    // Route cancel through the io_context thread so that it doesn't race
    // with handleTimerTick / scheduleNext executing on the io_context.
    boost::asio::dispatch(m_ioContext, [this]() { m_timer.cancel(); });

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
    catch (...)
    {
        BCOS_LOG(WARNING) << LOG_DESC("Unknown exception in Worker finishWorker")
                          << LOG_KV("threadName", m_threadName)
                          << LOG_KV("msg", boost::current_exception_diagnostic_information());
    }
}

void Worker::scheduleNext()
{
    if (m_workerState != WorkerState::Started)
    {
        return;
    }

    m_timer.expires_after(boost::asio::chrono::milliseconds(m_idleWaitMs));
    m_timer.async_wait([this](boost::system::error_code const& ec) { handleTimerTick(ec); });
}

void Worker::handleTimerTick(boost::system::error_code const& ec)
{
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
    catch (...)
    {
        hasException = true;
        BCOS_LOG(WARNING) << LOG_DESC("Unknown exception in Worker executeWorker")
                          << LOG_KV("threadName", m_threadName)
                          << LOG_KV("msg", boost::current_exception_diagnostic_information());
    }
    // FIB-111: backoff after exceptions to prevent tight CPU spin
    if (hasException && m_idleWaitMs == 0)
    {
        // Use a small backoff to avoid busy-looping on persistent exceptions
        m_timer.expires_after(boost::asio::chrono::milliseconds(10));
        m_timer.async_wait([this](boost::system::error_code const& ec) { handleTimerTick(ec); });
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
