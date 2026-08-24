#include "NewTimer.h"
#include "bcos-utilities/BoostLog.h"
#include <chrono>

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
    if (bool running = false; !m_running.compare_exchange_strong(running, true))
    {
        return;
    }

    // For direct execution (no steady_timer involved), execute synchronously.
    // This preserves the original behavior and avoids unnecessary dispatch.
    if (m_delayMS <= 0 && m_periodMS <= 0)
    {
        executeTask();
        return;
    }

    // Dispatch to io_context thread for thread safety (steady_timer is not
    // thread-safe).  If already on the io_context thread this runs
    // synchronously; otherwise it posts asynchronously.
    boost::asio::dispatch(*m_ioService, [weak = weak_from_this()]() {
        auto self = weak.lock();
        if (!self || !self->m_running)
        {
            return;
        }

        if (self->m_delayMS > 0)
        {
            self->startDelayTask();
        }
        else if (self->m_periodMS > 0)
        {
            self->startPeriodTask();
        }
    });
}
void bcos::timer::Timer::stop()
{
    if (bool running = true; !m_running.compare_exchange_strong(running, false))
    {
        return;
    }

    // Dispatch cancel to io_context thread to avoid racing with async_wait
    // handlers.  Lifecycle protected by weak_ptr.
    boost::asio::dispatch(*m_ioService, [weak = weak_from_this()]() {
        auto self = weak.lock();
        if (!self)
        {
            return;
        }

        if (self->m_delayHandler.has_value())
        {
            self->m_delayHandler->cancel();
        }

        if (self->m_timerHandler.has_value())
        {
            self->m_timerHandler->cancel();
        }
    });
}
void bcos::timer::Timer::startDelayTask()
{
    m_delayHandler.emplace(*m_ioService);

    auto self = weak_from_this();
    m_delayHandler->expires_after(std::chrono::milliseconds(m_delayMS));
    m_delayHandler->async_wait([self](const boost::system::error_code& error) {
        // The timer has been cancelled
        if (error == boost::asio::error::operation_aborted)
        {
            return;
        }

        auto timer = self.lock();
        if (!timer || !timer->m_running)
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
    m_timerHandler.emplace(*m_ioService);
    auto self = weak_from_this();
    m_timerHandler->expires_after(std::chrono::milliseconds(m_periodMS));
    m_timerHandler->async_wait([self](const boost::system::error_code& error) {
        // The timer has been cancelled
        if (error == boost::asio::error::operation_aborted)
        {
            return;
        }

        auto timer = self.lock();
        if (!timer || !timer->m_running)
        {
            return;
        }

        timer->executeTask();
        // Only re-arm if still running (stop() may have been called during
        // executeTask())
        if (timer->m_running)
        {
            timer->startPeriodTask();
        }
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
bcos::timer::Timer::Ptr bcos::timer::TimerFactory::createTimer(
    TimerTask&& _task, int _periodMS, int _delayMS)  // NOLINT
{
    auto timer = std::make_shared<Timer>(m_ioService, std::move(_task), _periodMS, _delayMS);
    return timer;
}
