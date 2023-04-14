//
// Created by Jimmy Shi on 2023/1/7.
//

#include "ShardingTransactionExecutive.h"
#include "bcos-table/src/ContractShardUtils.h"

using namespace bcos::executor;
using namespace bcos::storage;

ShardingTransactionExecutive::ShardingTransactionExecutive(const BlockContext& blockContext,
    std::string contractAddress, int64_t contextID, int64_t seq,
    const wasm::GasInjector& gasInjector, ThreadPool::Ptr pool, bool usePromise)
  : PromiseTransactionExecutive(
        pool, std::move(blockContext), std::move(contractAddress), contextID, seq, gasInjector),
    m_usePromise(usePromise)
{}

CallParameters::UniquePtr ShardingTransactionExecutive::start(CallParameters::UniquePtr input)
{
    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        EXECUTIVE_LOG(TRACE) << LOG_BADGE("Sharding")
                             << "ShardingTransactionExecutive start: " << input->toFullString()
                             << LOG_KV("usePromise", m_usePromise);
    }

    CallParameters::UniquePtr ret;
    if (m_usePromise)
    {
        ret = PromiseTransactionExecutive::start(std::move(input));
    }
    else
    {
        ret = CoroutineTransactionExecutive::start(std::move(input));
    }
    ret->contextID = contextID();
    ret->seq = seq();
    return ret;
}

CallParameters::UniquePtr ShardingTransactionExecutive::externalCall(
    CallParameters::UniquePtr input)
{
    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        EXECUTIVE_LOG(TRACE) << LOG_BADGE("Sharding")
                             << "ShardingTransactionExecutive externalCall: "
                             << input->toFullString() << LOG_KV("usePromise", m_usePromise);
    }

    // set DMC contextID and seq
    input->contextID = contextID();
    input->seq = seq();

    if (!std::empty(input->receiveAddress))
    {
        if (!m_shardName.has_value())
        {
            m_shardName = getContractShard(m_contractAddress);
        }

        auto toShardName = getContractShard(input->receiveAddress);
        if (toShardName != m_shardName.value())
        {
            EXECUTIVE_LOG(DEBUG) << LOG_BADGE("Sharding")
                                 << "ShardingTransactionExecutive call other shard: "
                                 << LOG_KV("toShard", toShardName)
                                 << LOG_KV("usePromise", m_usePromise)
                                 << LOG_KV("input", input->toFullString());
            if (m_usePromise)
            {
                return PromiseTransactionExecutive::externalCall(std::move(input));
            }
            else
            {
                return CoroutineTransactionExecutive::externalCall(std::move(input));
            }
        }
    }

    EXECUTIVE_LOG(DEBUG) << LOG_BADGE("Sharding") << "ShardingTransactionExecutive call local"
                         << input->toFullString();
    return TransactionExecutive::externalCall(std::move(input));
}

CallParameters::UniquePtr ShardingTransactionExecutive::resume()
{
    if (m_usePromise)
    {
        return PromiseTransactionExecutive::resume();
    }
    else
    {
        return CoroutineTransactionExecutive::resume();
    }
}

std::string ShardingTransactionExecutive::getContractShard(const std::string_view& contractAddress)
{
    auto tableName = getContractTableName(contractAddress, m_blockContext.isWasm());
    return ContractShardUtils::getContractShard(storage(), tableName);
}