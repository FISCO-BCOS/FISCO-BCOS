#include "NewTimer.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"

bcos::timer::Timer::Timer(std::shared_ptr<boost::asio::io_context> _ioService, TimerTask&& _task,
    int _periodMS,  // NOLINT
    int _delayMS)
  : m_ioService(std::move(_ioService)),
    m_timerTask(std::move(_task)),
    m_delayMS(_delayMS),
    m_periodMS(_periodMS)
{}
bcos::timer::Timer::~Timer()
{
    stop();
}
int bcos::timer::Timer::periodMS() const
{
    return m_periodMS;
}
int bcos::timer::Timer::delayMS() const
{
    return m_delayMS;
}
bcos::timer::TimerTask bcos::timer::Timer::timerTask() const
{
    return m_timerTask;
}
void bcos::timer::Timer::start()
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
void bcos::timer::Timer::stop()
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
void bcos::timer::Timer::startDelayTask()
{
    m_delayHandler = std::make_shared<boost::asio::steady_timer>(*(m_ioService));

    auto self = weak_from_this();
    m_delayHandler->expires_after(std::chrono::milliseconds(m_delayMS));
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
void bcos::timer::Timer::startPeriodTask()
{
    m_timerHandler = std::make_shared<boost::asio::steady_timer>(*m_ioService);
    auto self = weak_from_this();
    m_timerHandler->expires_after(std::chrono::milliseconds(m_periodMS));
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
void bcos::timer::Timer::executeTask()
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
bcos::timer::TimerFactory::TimerFactory(std::shared_ptr<boost::asio::io_context> _ioService)
  : m_ioService(std::move(_ioService))
{}
bcos::timer::TimerFactory::TimerFactory() : m_ioService(std::make_shared<boost::asio::io_context>())
{
    // No io_service object is provided, create io_service and the worker thread
    startThread();
}
bcos::timer::TimerFactory::~TimerFactory()
{
    stopThread();
}
bcos::timer::Timer::Ptr bcos::timer::TimerFactory::createTimer(
    TimerTask&& _task, int _periodMS, int _delayMS)  // NOLINT
{
    auto timer = std::make_shared<Timer>(m_ioService, std::move(_task), _periodMS, _delayMS);
    return timer;
}
void bcos::timer::TimerFactory::startThread()
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
                auto work = boost::asio::make_work_guard(*m_ioService);
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
            m_ioService->stop();
        }

        BCOS_LOG(INFO) << LOG_BADGE("startThread") << LOG_DESC("the timer thread stop");
    });
}
void bcos::timer::TimerFactory::stopThread()
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
