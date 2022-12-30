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

}  // namespace bcos::task