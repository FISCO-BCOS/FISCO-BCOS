#pragma once
#include "Scheduler.h"
#include <tbb/task_group.h>

namespace bcos::task
{

class TBBScheduler : public Scheduler
{
public:
    ~TBBScheduler() noexcept override { m_taskGroup.wait(); }

    void execute(CO_STD::coroutine_handle<> handle) override
    {
        m_taskGroup.run([handle]() {
            auto executeHandle = handle;
            executeHandle.resume();
        });
    }

private:
    tbb::task_group m_taskGroup;
};

}  // namespace bcos::task