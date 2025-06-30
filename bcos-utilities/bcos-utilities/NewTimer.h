

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
 * @brief implementation for TimerFactory
 * @file TimerFactory.h
 * @author: octopuswang
 * @date 2023-02-20
 */
#pragma once
#include "Common.h"
#include "bcos-utilities/BoostLog.h"
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <exception>
#include <memory>

namespace bcos::timer
{

using TimerTask = std::function<void()>;

class Timer : public std::enable_shared_from_this<Timer>
{
public:
    using Ptr = std::shared_ptr<Timer>;
    using ConstPtr = std::shared_ptr<Timer>;

    Timer(std::shared_ptr<boost::asio::io_context> _ioService, TimerTask&& _task,
        int _periodMS,  // NOLINT
        int _delayMS);
    Timer(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer& operator=(Timer&&) = delete;
    ~Timer();

    int periodMS() const;
    int delayMS() const;
    TimerTask timerTask() const;

    void start();
    void stop();

private:
    void startDelayTask();
    void startPeriodTask();
    void executeTask();

    bool m_running = false;
    std::shared_ptr<boost::asio::io_context> m_ioService;
    TimerTask m_timerTask;
    int m_delayMS;
    int m_periodMS;
    std::shared_ptr<boost::asio::steady_timer> m_delayHandler;
    std::shared_ptr<boost::asio::steady_timer> m_timerHandler;
};
class TimerFactory
{
public:
    using Ptr = std::shared_ptr<TimerFactory>;
    using ConstPtr = std::shared_ptr<TimerFactory>;

    TimerFactory(std::shared_ptr<boost::asio::io_context> _ioService);
    TimerFactory();
    TimerFactory(const TimerFactory&) = delete;
    TimerFactory(TimerFactory&&) = delete;
    TimerFactory& operator=(const TimerFactory&) = delete;
    TimerFactory& operator=(TimerFactory&&) = delete;
    ~TimerFactory();

    /**
     * @brief
     *
     * @param _task
     * @param _periodMS
     * @param delayMS
     * @return Timer::Ptr
     */
    Timer::Ptr createTimer(TimerTask&& _task, int _periodMS, int _delayMS = 0)  // NOLINT
        ;

private:
    void startThread();
    void stopThread();

    std::atomic_bool m_running = {false};
    std::string m_threadName = "timerFactory";
    std::unique_ptr<std::thread> m_worker;
    std::shared_ptr<boost::asio::io_context> m_ioService;
};

}  // namespace bcos::timer
