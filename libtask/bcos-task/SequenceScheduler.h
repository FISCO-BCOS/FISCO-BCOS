#pragma once

#include "Scheduler.h"
#include <bcos-utilities/NullLock.h>
#include <mutex>
#include <queue>
#include <variant>

namespace bcos::task
{

template <bool withMutex = false>
class SequenceScheduler : public Scheduler
{
private:
    std::queue<CO_STD::coroutine_handle<>> m_queue;
    bool m_running = false;

    using Lock = std::conditional_t<withMutex, std::unique_lock<std::mutex>, utilities::NullLock>;
    [[no_unique_address]] std::conditional_t<withMutex, std::mutex, std::monostate> m_mutex;

public:
    ~SequenceScheduler() noexcept override = default;

    void execute(CO_STD::coroutine_handle<> handle) override
    {
        {
            Lock lock(m_mutex);
            m_queue.push(handle);
            if (m_running)
            {
                return;
            }
            m_running = true;
        }

        while (true)
        {
            Lock lock(m_mutex);
            if (m_queue.empty())
            {
                m_running = false;
                break;
            }

            auto executeHandle = m_queue.front();
            m_queue.pop();
            lock.unlock();

            executeHandle.resume();
        }
    }
};

}  // namespace bcos::task