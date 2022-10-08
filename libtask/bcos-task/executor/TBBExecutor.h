#pragma once

#include "../Coroutine.h"
#include <tbb/task_group.h>
#include <coroutine>

namespace bcos::task::executor
{

class TBBExecutor
{
public:
    void execute(CO_STD::coroutine_handle<> handle)
    {
        m_taskGroup.run([m_handle = std::move(handle)]() { m_handle.resume(); });
    }

private:
    tbb::task_group m_taskGroup;
};
}  // namespace bcos::task::executor