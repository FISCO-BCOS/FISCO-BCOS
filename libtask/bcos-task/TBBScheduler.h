#pragma once
#include "Coroutine.h"
#include <oneapi/tbb/task_group.h>

namespace bcos::task::tbb
{

class TBBScheduler
{
private:
    oneapi::tbb::task_group& m_taskGroup;

public:
    TBBScheduler(oneapi::tbb::task_group& taskGroup) : m_taskGroup(taskGroup) {}

    constexpr static bool await_ready() noexcept { return false; }
    void await_suspend([[maybe_unused]] CO_STD::coroutine_handle<> handle) noexcept
    {
        m_taskGroup.run([handle]() { const_cast<CO_STD::coroutine_handle<>&>(handle).resume(); });
    }
    constexpr static void await_resume() noexcept {}
};

}  // namespace bcos::task::tbb