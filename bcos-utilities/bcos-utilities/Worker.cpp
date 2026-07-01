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

using namespace bcos;

Worker::~Worker()
{
    stopWorking();
    // stopWorking() may only post the cancel (when called off the io_context
    // thread). Call cancel() synchronously here as well so the timer is
    // guaranteed quiesced before its destructor runs — macOS' kqueue-backed
    // steady_timer can race with a live io_context otherwise.
    m_timer.cancel();
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
    boost::asio::dispatch(m_ioContext, [weak = weak_from_this()]() {
        auto self = weak.lock();
        if (self && self->m_workerState == WorkerState::Started)
        {
            self->scheduleNext();
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
    if (m_ioContext.get_executor().running_in_this_thread())
    {
        m_timer.cancel();
    }
    else
    {
        boost::asio::post(m_ioContext, [weak = weak_from_this()]() {
            auto self = weak.lock();
            if (self)
            {
                self->m_timer.cancel();
            }
        });
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
    m_timer.async_wait([weak = weak_from_this()](boost::system::error_code const& ec) {
        auto self = weak.lock();
        if (self)
        {
            self->handleTimerTick(ec);
        }
    });
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
        m_timer.async_wait([weak = weak_from_this()](boost::system::error_code const& ec) {
            auto self = weak.lock();
            if (self)
            {
                self->handleTimerTick(ec);
            }
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
        // Cancel any pending timer and immediately trigger one worker cycle
        // via handleTimerTick, which already contains executeWorker(),
        // exception handling, FIB-111 backoff, and scheduleNext().
        // Use post() instead of dispatch() to avoid re-entrant execution
        // of executeWorker() when notify() is called from within a Timer
        // callback on the io_context thread (e.g. onTimeout() ->
        // broadcastViewChangeReq() -> onReceivePBFTMessage() -> notify()).
        // dispatch() would run handleTimerTick synchronously, causing
        // nested PBFT message processing that breaks consensus ordering.
        boost::asio::post(m_ioContext, [weak = weak_from_this()]() {
            auto self = weak.lock();
            if (self && self->m_workerState == WorkerState::Started)
            {
                self->handleTimerTick(boost::system::error_code());
            }
        });
    }
}
