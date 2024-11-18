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

using namespace bcos;

bcos::Timer::Timer(
    std::shared_ptr<boost::asio::io_service> ioService, int64_t timeout, std::string threadName)
  : m_timeout(timeout),
    m_working(true),
    m_ioService(std::move(ioService)),
    m_timer(*m_ioService),
    m_threadName(std::move(threadName)),
    m_borrowedIoService(true)
{}

bcos::Timer::Timer(int64_t _timeout, std::string _threadName)
  : m_timeout(_timeout),
    m_working(true),
    m_ioService(std::make_shared<boost::asio::io_service>()),
    m_work(*m_ioService),
    m_timer(*m_ioService),
    m_threadName(std::move(_threadName)),
    m_worker(std::make_unique<std::thread>([&]() {
        bcos::pthread_setThreadName(m_threadName);
        while (m_working)
        {
            try
            {
                m_ioService->run();
            }
            catch (std::exception const& e)
            {
                BCOS_LOG(WARNING) << LOG_DESC("Exception in Worker Thread of timer")
                                  << LOG_KV("message", boost::diagnostic_information(e));
            }
            m_ioService->reset();
        }
    }))
{}

void Timer::start()
{
    if (!m_working)
    {
        return;
    }
    try
    {
        startTimer();
    }
    catch (std::exception const& e)
    {
        BCOS_LOG(WARNING) << LOG_DESC("startTimer exception")
                          << LOG_KV("message", boost::diagnostic_information(e));
    }
}

void Timer::startTimer()
{
    if (bool running = false; !m_running.compare_exchange_strong(running, true))
    {
        return;
    }
    m_timer.expires_from_now(std::chrono::milliseconds(adjustTimeout()));
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
        // cancel the timer
        m_timer.cancel();
    }
}

void Timer::destroy()
{
    if (!m_working)
    {
        return;
    }
    m_working = false;
    stop();
    if (!m_borrowedIoService)
    {
        m_ioService->stop();
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
