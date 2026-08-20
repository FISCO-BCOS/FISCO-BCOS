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

// windows.h -- dragged in by <boost/asio/...> above -- defines ERROR as 0, which turns the
// BCOS_LOG(ERROR) calls below into bcos::LogLevel::0. BoostLog.h undefs it, but BoostLog.h is
// #pragma once: in a unity build some earlier file in the same blob has already included it, back
// when ERROR was still undefined, so its #undef ran as a no-op and cannot run again here. Undo it
// after every include in this file, so no later asio header can re-introduce it before use.
#ifdef ERROR
#undef ERROR
#endif

using namespace bcos;

namespace
{
// Upper bound on how long a stop/destroy will wait for the io_context to drain. Exceeding it is
// always a bug (a wedged handler, or an io_context nobody runs), so it is logged rather than
// silently tolerated -- but it is bounded so a wedge degrades into a stall, never a deadlock.
constexpr auto c_drainTimeout = std::chrono::seconds(5);
}  // namespace

Worker::~Worker()
{
    stopWorking();
    // stopWorking() now owns the main barrier; this one is the backstop for the cases it
    // cannot cover: it returned early because m_workerState was already Stopped (e.g.
    // Sealer::stop() ran first) and handlers landed afterwards; it took one of its two
    // no-wait branches; or its own wait timed out. Pending [this]-capturing handlers
    // (operation_aborted from cancel, notify() posts) may still be queued in those cases,
    // so post a drain barrier and wait before *this is freed.
    if (!m_ioContext.stopped() && !m_ioContext.get_executor().running_in_this_thread())
    {
        auto drainPromise = std::make_shared<std::promise<void>>();
        auto drainFuture = drainPromise->get_future();
        boost::asio::post(m_ioContext, [drainPromise]() { drainPromise->set_value(); });
        auto drainStatus = drainFuture.wait_for(c_drainTimeout);
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

    // Cancel the timer and WAIT until the io_context has drained, so that stopWorking()
    // returning means "no timer-driven executeWorker() is in flight, and nothing this Worker
    // queued is still pending".
    //
    // What this does NOT cover: notify() (below) reads m_workerState and posts in two separate
    // steps, so a concurrent notify() that passed its check just before the CAS can still land a
    // handler after this barrier has drained. Closing that needs the handlers to hold ownership
    // of the object (weak_from_this in every posted lambda), which is a separate change.
    //
    // The wait is not optional. The CAS above only stops FUTURE ticks from entering
    // executeWorker(); a tick that already entered keeps running on the io_context
    // thread. ~Worker() does drain the queue, but by C++ destruction order it runs
    // AFTER the derived class has destroyed its own members -- so a derived
    // executeWorker() (e.g. Sealer's, which dereferences m_sealingManager) would be
    // touching freed memory long before that drain is reached. The barrier has to be
    // here, while the whole object is still intact.
    if (m_ioContext.get_executor().running_in_this_thread())
    {
        // We ARE the io_context thread, so the only handler that could be in flight is the one
        // calling us: there is nothing to wait for, and waiting would deadlock against
        // ourselves. Cancelling here is safe precisely because we are on that thread.
        m_timer.cancel();
    }
    else if (!m_ioContext.stopped())
    {
        auto drainPromise = std::make_shared<std::promise<void>>();
        auto drainFuture = drainPromise->get_future();
        // The barrier is posted from INSIDE the cancel handler on purpose. It is
        // m_timer.cancel() that queues the outstanding async_wait's operation_aborted
        // completion, so a barrier posted from this thread alongside the cancel would
        // sit AHEAD of that completion and let stopWorking() return with a handler
        // still queued. Posting it after the cancel call puts it behind.
        boost::asio::post(m_ioContext, [this, drainPromise]() {
            m_timer.cancel();
            boost::asio::post(m_ioContext, [drainPromise]() { drainPromise->set_value(); });
        });
        // Deliberately wait_for() and never get(): if the outer handler is destroyed without
        // running, the future is ready-with-broken_promise, and get() would rethrow that inside
        // whoever is stopping us. Only readiness matters here.
        if (drainFuture.wait_for(c_drainTimeout) != std::future_status::ready)
        {
            BCOS_LOG(ERROR) << LOG_DESC("Worker::stopWorking() timed out draining handlers")
                            << LOG_KV("threadName", m_threadName);
        }
    }
    // else: the io_context is stopped. No handler is running, none ever will be, and the
    // operation_aborted completion a cancel would queue could never be delivered either -- so
    // there is nothing to cancel and nothing to drain. Touching m_timer from this thread would
    // only add a cross-thread access to an object asio documents as unsafe to share.

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
    // NOTE: this check and the post below are not atomic, so a caller that reads Started here can
    // still land its handler after stopWorking() has drained and the object has been destroyed.
    // stopWorking()'s barrier does NOT cover that window; closing it needs the posted lambdas to
    // hold ownership (weak_from_this) rather than a raw this.
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
