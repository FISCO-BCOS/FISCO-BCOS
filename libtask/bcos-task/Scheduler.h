#pragma once
#include "Coroutine.h"

namespace bcos::task
{

class Scheduler
{
public:
    virtual ~Scheduler() noexcept = default;
    virtual void execute(CO_STD::coroutine_handle<> handle) = 0;
};

struct CurrentScheduler
{
    static constexpr bool await_ready() noexcept { return false; }

    template <class Promise>
    auto await_suspend(CO_STD::coroutine_handle<Promise> handle) noexcept
    {
        m_scheduler = handle.promise().m_scheduler;
        return handle;
    }

    Scheduler* await_resume() const noexcept { return m_scheduler; }

    Scheduler* m_scheduler;
};

}  // namespace bcos::task