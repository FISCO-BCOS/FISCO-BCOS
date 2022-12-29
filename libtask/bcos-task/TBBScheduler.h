#pragma once
#include "Scheduler.h"
#include "Wait.h"
#include <tbb/task_group.h>
#include <queue>
#include <type_traits>

namespace bcos::task
{

class TBBScheduler : public Scheduler
{
public:
    ~TBBScheduler() noexcept override { m_taskGroup.wait(); }

    void execute(CO_STD::coroutine_handle<> handle) override
    {
        m_taskGroup.run([handle]() { handle.resume(); });
    }

    template <class Task>
    requires std::is_rvalue_reference_v<Task>
    typename Task::ReturnType run(Task&& task)
    {
        task.setScheduler(this);
        m_taskGroup.run([m_task = std::forward<Task>(task)]() { m_task.run(); });
    }

    tbb::task_group m_taskGroup;
};

}  // namespace bcos::task