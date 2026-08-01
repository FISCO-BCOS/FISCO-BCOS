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

#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <chrono>
#include <exception>
#include <future>
// Boost.Asio may pull windows.h (ERROR macro). Include BoostLog last so BCOS_LOG(ERROR) works.
#include "BoostLog.h"

using namespace bcos;

Worker::~Worker()
{
    stopWorking();
    // stopWorking() may return early when m_workerState is already
    // Stopped (e.g. Sealer::stop() called stopWorking() before the
    // destructor).  In that case pending [this]-capturing handlers
    // (operation_aborted from cancel, notify() posts) may still be
    // queued in the io_context.  Post a drain barrier and wait so
    // that all such handlers are processed before *this is freed.
    if (!m_ioContext.stopped() && !m_ioContext.get_executor().running_in_this_thread())
    {
        auto drainPromise = std::make_shared<std::promise<void>>();
        auto drainFuture = drainPromise->get_future();
        boost::asio::post(m_ioContext, [drainPromise]() { drainPromise->set_value(); });
        auto drainStatus = drainFuture.wait_for(std::chrono::seconds(5));
        if (drainStatus != std::future_status::ready)
        {
            BCOS_LOG(ERROR) << LOG_DESC("Worker::~Worker() timed out waiting for handler drain")
                            << LOG_KV("threadName", m_threadName);
        }
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
    // must only happen on the io_context thread.  dispatch() runs the
    // handler synchronously if we are already on the io_context thread;
    // otherwise it behaves like post() and queues it for later execution.
    boost::asio::dispatch(m_ioContext, [this]() {
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

    // Cancel the timer.  If we are already on the io_context thread we
    // can mutate m_timer directly; otherwise we post the cancel.
    // No need to wait — ~Worker() drains all [this]-capturing handlers
    // before the object can be freed, and any timer tick that fires
    // between now and the cancel will early-return on !Started.
    if (m_ioContext.get_executor().running_in_this_thread())
    {
        m_timer.cancel();
    }
    else
    {
        boost::asio::post(m_ioContext, [this]() { m_timer.cancel(); });
    }

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
    if (ec == boost::asio::error::operation_aborted || m_workerState != WorkerState::Started)
    {
        return;  // Timer was cancelled
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
        // Cancel any pending timer and immediately trigger one worker cycle
        // via handleTimerTick, which already contains executeWorker(),
        // exception handling, FIB-111 backoff, and scheduleNext().
        // Use post() instead of dispatch() to avoid re-entrant execution
        // of executeWorker() when notify() is called from within a Timer
        // callback on the io_context thread (e.g. onTimeout() ->
        // broadcastViewChangeReq() -> onReceivePBFTMessage() -> notify()).
        // dispatch() would run handleTimerTick synchronously, causing
        // nested PBFT message processing that breaks consensus ordering.
        boost::asio::post(m_ioContext, [this]() {
            if (m_workerState == WorkerState::Started)
            {
                handleTimerTick(boost::system::error_code());
            }
        });
    }
}
