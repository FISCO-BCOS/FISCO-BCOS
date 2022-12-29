#pragma once

#pragma once
#include "Scheduler.h"
#include <tbb/task_group.h>
#include <queue>
#include <type_traits>

namespace bcos::task
{

class MonoScheduler : public Scheduler
{
public:
    static MonoScheduler instance;

    MonoScheduler() = default;
    MonoScheduler(const MonoScheduler&) = default;
    MonoScheduler(MonoScheduler&&) = default;
    MonoScheduler& operator=(const MonoScheduler&) = default;
    MonoScheduler& operator=(MonoScheduler&&) = default;
    ~MonoScheduler() noexcept override = default;

    void execute(CO_STD::coroutine_handle<> handle) override
    {
        m_nextHandle = handle;

        if (!m_started)
        {
            m_started = true;
            while (m_nextHandle)
            {
                auto nextHandle = m_nextHandle;
                m_nextHandle = {};
                nextHandle.resume();
            }
        }
    }

    CO_STD::coroutine_handle<> m_nextHandle;
    bool m_started = false;
};

}  // namespace bcos::task