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
 * @file Timer.h
 * @author: yujiechen
 * @date 2021-04-26
 */
#pragma once
#include "Common.h"
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/exception/diagnostic_information.hpp>
namespace dev
{
class Timer : public std::enable_shared_from_this<Timer>
{
public:
    using Ptr = std::shared_ptr<Timer>;
    explicit Timer(uint64_t _timeout, std::string const& _threadName = "timer")
      : m_timeout(_timeout),
        m_ioService(std::make_shared<boost::asio::io_service>()),
        m_timer(std::make_shared<boost::asio::steady_timer>(*m_ioService)),
        m_work(*m_ioService),
        m_threadName(_threadName)
    {
        m_working = true;
        m_worker.reset(new std::thread([&]() {
            pthread_setThreadName(m_threadName);
            while (m_working)
            {
                try
                {
                    m_ioService->run();
                }
                catch (std::exception const& e)
                {
                    LOG(WARNING) << LOG_DESC("Exception in Worker Thread of timer")
                                 << LOG_KV("error", boost::diagnostic_information(e));
                }
                m_ioService->reset();
            }
        }));
    }

    virtual ~Timer() { destroy(); }

    virtual void destroy();
    virtual void start();
    virtual void stop();
    virtual void restart()
    {
        if (!m_working)
        {
            return;
        }
        stop();
        start();
    }

    virtual void reset(uint64_t _timeout)
    {
        m_timeout = _timeout;
        restart();
    }

    virtual bool running() { return m_running; }
    virtual int64_t timeout() { return m_timeout; }

    virtual void registerTimeoutHandler(std::function<void()> _timeoutHandler)
    {
        m_timeoutHandler = _timeoutHandler;
    }

protected:
    virtual void startTimer();

    // invoked everytime when it reaches the timeout
    virtual void run()
    {
        if (m_timeoutHandler)
        {
            m_timeoutHandler();
        }
    }
    // adjust the timeout
    virtual uint64_t adjustTimeout() { return m_timeout; }
    std::atomic<uint64_t> m_timeout = {0};

    std::atomic_bool m_running = {false};
    std::atomic_bool m_working = {false};

    std::shared_ptr<boost::asio::io_service> m_ioService;
    std::shared_ptr<boost::asio::steady_timer> m_timer;
    std::unique_ptr<std::thread> m_worker;

    std::function<void()> m_timeoutHandler;
    // m_work ensures that io_service's run() function will not exit while work is underway
    boost::asio::io_service::work m_work;

    std::string m_threadName;
};
}  // namespace dev