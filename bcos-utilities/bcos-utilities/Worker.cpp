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
#include <chrono>
#include <exception>
#include <future>

using namespace bcos;

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

    // The CAS above already changed m_workerState to Stopped, which
    // causes in-flight handlers (handleTimerTick, scheduleNext) to
    // bail out at their workerState checks.

    // Cancel the timer.  If we are already on the io_context thread we
    // can mutate m_timer directly; otherwise we post the cancel and
    // synchronize on its completion via a promise/future barrier.  We
    // cannot stop the io_context because it is shared with other users.
    if (m_ioContext.get_executor().running_in_this_thread())
    {
        // On the io_context thread — cancel is safe to call directly.
        m_timer.cancel();
    }
    else
    {
        // Not on the io_context thread — post cancel + barrier and wait.
        auto syncPromise = std::make_shared<std::promise<void>>();
        auto syncFuture = syncPromise->get_future();
        boost::asio::post(m_ioContext, [this, syncPromise = std::move(syncPromise)]() {
            m_timer.cancel();
            syncPromise->set_value();
        });

        // Block until the io_context thread has processed our cancel,
        // but bound the wait to avoid a hard hang when the io_context
        // is no longer being serviced (e.g. teardown ordering bug).
        //
        // Note: the cancellation itself is synchronous, but
        // boost::asio::steady_timer::cancel() queues an
        // operation_aborted completion that runs *after* set_value().
        // That one handler may escape this barrier.  It is safe because
        // handleTimerTick() early-returns on operation_aborted before
        // touching any member.
        auto status = syncFuture.wait_for(std::chrono::seconds(5));
        if (status != std::future_status::ready)
        {
            BCOS_LOG(ERROR)
                << LOG_DESC(
                       "Worker::stopWorking() timed out waiting for io_context to process "
                       "cancel")
                << LOG_KV("threadName", m_threadName);
        }
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
                m_timer.cancel();
                handleTimerTick(boost::system::error_code());
            }
        });
    }
}
