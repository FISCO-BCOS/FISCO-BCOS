

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

namespace bcos
{

namespace timer
{

using TimerTask = std::function<void()>;

class Timer : public std::enable_shared_from_this<Timer>
{
public:
    using Ptr = std::shared_ptr<Timer>;
    using ConstPtr = std::shared_ptr<Timer>;

    Timer(std::shared_ptr<boost::asio::io_service> _ioService, TimerTask&& _task,
        int _periodMS,  // NOLINT
        int _delayMS)
      : m_ioService(std::move(_ioService)),
        m_timerTask(std::move(_task)),
        m_delayMS(_delayMS),
        m_periodMS(_periodMS)
    {}
    Timer(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer& operator=(Timer&&) = delete;
    ~Timer() { stop(); }

    int periodMS() const { return m_periodMS; }
    int delayMS() const { return m_delayMS; }
    TimerTask timerTask() const { return m_timerTask; }

    void start()
    {
        if (m_running)
        {
            return;
        }
        m_running = true;

        if (m_delayMS > 0)
        {  // delay handle
            startDelayTask();
        }
        else if (m_periodMS > 0)
        {
            // periodic task handle
            startPeriodTask();
        }
        else
        {
            // execute the task directly
            executeTask();
        }
    }

    void stop()
    {
        if (!m_running)
        {
            return;
        }

        if (m_delayHandler)
        {
            m_delayHandler->cancel();
        }

        if (m_timerHandler)
        {
            m_timerHandler->cancel();
        }
    }

private:
    void startDelayTask()
    {
        m_delayHandler = std::make_shared<boost::asio::deadline_timer>(
            *(m_ioService), boost::posix_time::milliseconds(m_delayMS));

        auto self = weak_from_this();
        m_delayHandler->async_wait([self](const boost::system::error_code& e) {
            auto timer = self.lock();
            if (!timer)
            {
                return;
            }

            if (timer->periodMS() > 0)
            {
                timer->startPeriodTask();
            }
            else
            {
                timer->executeTask();
            }
        });
    }

    void startPeriodTask()
    {
        m_timerHandler = std::make_shared<boost::asio::deadline_timer>(
            *(m_ioService), boost::posix_time::milliseconds(m_periodMS));
        auto self = weak_from_this();
        m_timerHandler->async_wait([self](const boost::system::error_code& e) {
            auto timer = self.lock();
            if (!timer)
            {
                return;
            }

            timer->executeTask();
            timer->startPeriodTask();
        });
    }

    void executeTask()
    {
        try
        {
            if (m_timerTask)
            {
                m_timerTask();
            }
        }
        catch (const std::exception& _e)
        {
            BCOS_LOG(WARNING) << LOG_BADGE("Timer") << LOG_DESC("timer task exception")
                              << LOG_KV("what", _e.what());
        }
    }

    bool m_running = false;
    std::shared_ptr<boost::asio::io_service> m_ioService;
    TimerTask m_timerTask;
    int m_delayMS;
    int m_periodMS;
    std::shared_ptr<boost::asio::deadline_timer> m_delayHandler;
    std::shared_ptr<boost::asio::deadline_timer> m_timerHandler;
};
class TimerFactory
{
public:
    using Ptr = std::shared_ptr<TimerFactory>;
    using ConstPtr = std::shared_ptr<TimerFactory>;

    TimerFactory(std::shared_ptr<boost::asio::io_service> _ioService)
      : m_ioService(std::move(_ioService))
    {}
    TimerFactory() : m_ioService(std::make_shared<boost::asio::io_service>())
    {
        // No io_service object is provided, create io_service and the worker thread
        startThread();
    }
    TimerFactory(const TimerFactory&) = delete;
    TimerFactory(TimerFactory&&) = delete;
    TimerFactory& operator=(const TimerFactory&) = delete;
    TimerFactory& operator=(TimerFactory&&) = delete;
    ~TimerFactory() { stopThread(); }

    /**
     * @brief
     *
     * @param _task
     * @param _periodMS
     * @param delayMS
     * @return Timer::Ptr
     */
    Timer::Ptr createTimer(TimerTask&& _task, int _periodMS, int _delayMS = 0)  // NOLINT
    {
        auto timer = std::make_shared<Timer>(m_ioService, std::move(_task), _periodMS, _delayMS);
        return timer;
    }

private:
    void startThread()
    {
        if (m_worker)
        {
            return;
        }
        m_running = true;

        BCOS_LOG(INFO) << LOG_BADGE("startThread") << LOG_DESC("start the timer thread");

        m_worker = std::make_unique<std::thread>([this]() {
            bcos::pthread_setThreadName(m_threadName);
            BCOS_LOG(INFO) << LOG_BADGE("startThread") << LOG_DESC("the timer thread start")
                           << LOG_KV("threadName", m_threadName);
            while (m_running)
            {
                try
                {
                    m_ioService->run();
                    if (!m_running)
                    {
                        break;
                    }
                }
                catch (std::exception const& e)
                {
                    BCOS_LOG(WARNING) << LOG_BADGE("startThread")
                                      << LOG_DESC("Exception in Worker Thread of timer")
                                      << LOG_KV("message", boost::diagnostic_information(e));
                }
                m_ioService->reset();
            }

            BCOS_LOG(INFO) << LOG_BADGE("startThread") << LOG_DESC("the timer thread stop");
        });
    }

    void stopThread()
    {
        if (!m_worker)
        {
            return;
        }
        m_running = false;
        BCOS_LOG(INFO) << LOG_BADGE("stopThread") << LOG_DESC("stop the timer thread");

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

    std::atomic_bool m_running = {false};
    std::string m_threadName = "timerFactory";
    std::unique_ptr<std::thread> m_worker = nullptr;
    std::shared_ptr<boost::asio::io_service> m_ioService = nullptr;
};

}  // namespace timer
}  // namespace bcos