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
#include <boost/asio.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>
#include <optional>

namespace bcos
{
class Timer : public std::enable_shared_from_this<Timer>
{
public:
    Timer(std::shared_ptr<boost::asio::io_service> ioService, int64_t timeout,
        std::string threadName);
    Timer(int64_t _timeout, std::string _threadName);

    virtual ~Timer() noexcept;

    virtual void destroy();
    virtual void start();
    virtual void stop();
    virtual void restart();

    virtual void reset(int64_t _timeout);

    virtual bool running();
    virtual int64_t timeout();

    virtual void registerTimeoutHandler(std::function<void()> _timeoutHandler);

protected:
    virtual void startTimer();

    // invoked everytime when it reaches the timeout
    virtual void run();
    // adjust the timeout
    virtual uint64_t adjustTimeout();
    std::atomic_int64_t m_timeout = 0;

    std::atomic_bool m_running = false;
    std::atomic_bool m_working = false;

    std::shared_ptr<boost::asio::io_service> m_ioService;
    boost::asio::steady_timer m_timer;
    std::unique_ptr<std::thread> m_worker;

    std::function<void()> m_timeoutHandler;
    // m_work ensures that io_service's run() function will not exit while work is underway
    std::optional<boost::asio::io_service::work> m_work;
    std::string m_threadName;
    bool m_borrowedIoService = false;
};
}  // namespace bcos