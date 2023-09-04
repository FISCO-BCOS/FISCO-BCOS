#pragma once
#include "bcos-executor/src/executive/TransactionExecutive.h"

namespace bcos::transaction_executor
{

class ExecutiveWrapper : public bcos::executor::TransactionExecutive
{
public:
    executor::CallParameters::UniquePtr externalCall(
        executor::CallParameters::UniquePtr input) override
    {}
};
}  // namespace bcos::transaction_executor