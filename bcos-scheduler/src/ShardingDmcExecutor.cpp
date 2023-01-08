//
// Created by Jimmy Shi on 2023/1/7.
//

#include "ShardingDmcExecutor.h"
using namespace bcos::scheduler;


void ShardingDmcExecutor::executorCall(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    m_executor->call(std::move(input), std::move(callback));
};

void ShardingDmcExecutor::executorExecuteTransactions(std::string contractAddress,
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,

    // called every time at all tx stop( pause or finish)
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    m_executor->executeTransactions(
        std::move(contractAddress), std::move(inputs), std::move(callback));
};