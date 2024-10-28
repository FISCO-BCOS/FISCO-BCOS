#pragma once

#include "EVMCResult.h"

static void cleanEVMCResult(evmc_result& from)
{
    from.release = nullptr;
    from.output_data = nullptr;
    from.output_size = 0;
}

bcos::transaction_executor::EVMCResult::EVMCResult(evmc_result&& from) : evmc_result(from)
{
    cleanEVMCResult(from);
}

bcos::transaction_executor::EVMCResult::EVMCResult(EVMCResult&& from) noexcept : evmc_result(from)
{
    cleanEVMCResult(from);
}

bcos::transaction_executor::EVMCResult& bcos::transaction_executor::EVMCResult::operator=(
    EVMCResult&& from) noexcept
{
    evmc_result::operator=(from);
    cleanEVMCResult(from);
    return *this;
}

bcos::transaction_executor::EVMCResult::~EVMCResult() noexcept
{
    if (release != nullptr)
    {
        release(static_cast<evmc_result*>(this));
        release = nullptr;
        output_data = nullptr;
        output_size = 0;
    }
}
