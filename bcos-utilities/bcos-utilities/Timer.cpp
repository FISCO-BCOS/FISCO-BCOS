/**
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
 * @brief implementation for Timer
 * @file Timer.cpp
 * @author: yujiechen
 * @date 2021-04-26
 */
#include "Timer.h"
#include "BoostLog.h"
#include "Common.h"
#include "bcos-utilities/Overloaded.h"
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <future>

using namespace bcos;

bcos::Timer::Timer(boost::asio::io_context& ioService, int64_t timeout, std::string threadName)
  : m_timeout(timeout),
    m_working(true),
    m_ioService(std::addressof(ioService)),
    m_timer(this->ioService()),
    m_threadName(std::move(threadName))
{}

void Timer::start()
{
    if (!m_working)
    {
        return;
    }

    // startTimer() mutates m_timer (expires_after / async_wait), which
    // must only happen on the io_context thread.  dispatch() runs the
    // handler synchronously if we are already on the io_context thread;
    // otherwise it behaves like post() and queues it for later execution.
    // Capture a weak_ptr so the handler safely bails out if the Timer is
    // destroyed before the io_context processes this dispatch.
    boost::asio::dispatch(ioService(), [weak = weak_from_this()]() {
        auto self = weak.lock();
        if (!self || !self->m_working)
        {
            return;
        }
        try
        {
            self->startTimer();
        }
        catch (std::exception const& e)
        {
            BCOS_LOG(WARNING) << LOG_DESC("startTimer exception")
                              << LOG_KV("threadName", self->m_threadName)
                              << LOG_KV("message", boost::diagnostic_information(e));
        }
    });
}

void Timer::startTimer()
{
    if (bool running = false; !m_running.compare_exchange_strong(running, true))
    {
        return;
    }
    m_timer.expires_after(std::chrono::milliseconds(adjustTimeout()));
    // calls the timeout handler
    m_timer.async_wait([timerWeak = std::weak_ptr<Timer>(shared_from_this())](
                           const boost::system::error_code& error) {
        // the timer has been cancelled
        if (error == boost::asio::error::operation_aborted)
        {
            return;
        }
        if (error)
        {
            BCOS_LOG(WARNING) << LOG_DESC("Timer async_wait error") << LOG_KV("message", error);
            return;
        }
        try
        {
            if (auto timer = timerWeak.lock())
            {
                timer->run();
            }
        }
        catch (std::exception const& e)
        {
            BCOS_LOG(WARNING) << LOG_DESC("calls timeout handler failed")
                              << LOG_KV("message", boost::diagnostic_information(e));
        }
    });
}

// stop the timer
void Timer::stop()
{
    if (!m_working)
    {
        return;
    }

    if (bool running = true; m_running.compare_exchange_strong(running, false))
    {
        // Cancel the timer.  If we are already on the io_context thread we
        // can mutate m_timer directly; otherwise we post the cancel and
        // synchronize on its completion via a promise/future barrier.
        if (ioService().get_executor().running_in_this_thread())
        {
            // On the io_context thread — cancel is safe to call directly.
            m_timer.cancel();
        }
        else
        {
            // Not on the io_context thread — post cancel + barrier and wait.
            auto syncPromise = std::make_shared<std::promise<void>>();
            auto syncFuture = syncPromise->get_future();
            boost::asio::post(ioService(), [this, syncPromise = std::move(syncPromise)]() {
                m_timer.cancel();
                syncPromise->set_value();
            });

            // Block until the io_context thread has processed our cancel.
            // No deadlock risk: we only take this path when NOT on the
            // io_context thread (checked above with running_in_this_thread()).
            syncFuture.wait();
        }
    }
}

void Timer::destroy()
{
    if (!m_working)
    {
        return;
    }
    // Cancel the timer first, then mark as not working.
    // stop() internally checks m_working; we must not clear it beforehand.
    stop();
    m_working = false;
    if (!borrowedIoService())
    {
        ioService().stop();
        if (m_worker->get_id() != std::this_thread::get_id())
        {
            m_worker->join();
            m_worker.reset();
        }
        else
        {
            m_worker->detach();
        }
    }
}
bcos::Timer::~Timer() noexcept
{
    destroy();
}
void bcos::Timer::restart()
{
    stop();
    start();
}
void bcos::Timer::reset(int64_t _timeout)
{
    m_timeout = _timeout;
    restart();
}
bool bcos::Timer::running()
{
    return m_running;
}
int64_t bcos::Timer::timeout()
{
    return m_timeout;
}
void bcos::Timer::registerTimeoutHandler(std::function<void()> _timeoutHandler)
{
    m_timeoutHandler = std::move(_timeoutHandler);
}
void bcos::Timer::run()
{
    if (m_timeoutHandler)
    {
        m_timeoutHandler();
    }
}
uint64_t bcos::Timer::adjustTimeout()
{
    return m_timeout;
}
void bcos::Timer::setTimeout(int64_t timeout)
{
    m_timeout = timeout;
}
bool bcos::Timer::borrowedIoService() const
{
    return std::holds_alternative<boost::asio::io_context*>(m_ioService);
}
boost::asio::io_context& bcos::Timer::ioService()
{
    return std::visit(bcos::overloaded([](boost::asio::io_context* ptr) -> auto& { return *ptr; },
                          [](boost::asio::io_context& ref) -> auto& { return ref; }),
        m_ioService);
}
