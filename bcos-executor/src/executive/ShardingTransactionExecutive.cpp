//
// Created by Jimmy Shi on 2023/1/7.
//

#include "ShardingTransactionExecutive.h"
#include "bcos-table/src/ContractShardUtils.h"

using namespace bcos::executor;
using namespace bcos::storage;


ShardingTransactionExecutive::ShardingTransactionExecutive(std::weak_ptr<BlockContext> blockContext,
    std::string contractAddress, int64_t contextID, int64_t seq,
    std::shared_ptr<wasm::GasInjector>& gasInjector)
  : CoroutineTransactionExecutive(
        std::move(blockContext), std::move(contractAddress), contextID, seq, gasInjector)
{}

CallParameters::UniquePtr ShardingTransactionExecutive::start(CallParameters::UniquePtr input)
{
    auto ret = CoroutineTransactionExecutive::start(std::move(input));
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
                             << input->toFullString();
    }

    // TODO: remove this log
    EXECUTIVE_LOG(INFO) << LOG_BADGE("Sharding")
                        << "ShardingTransactionExecutive externalCall: " << input->toFullString();

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
            EXECUTIVE_LOG(INFO) << LOG_BADGE("Sharding")
                                << "ShardingTransactionExecutive call other shard: "
                                << LOG_KV("toShard", toShardName) << input->toFullString();
            return CoroutineTransactionExecutive::externalCall(std::move(input));
        }
    }

    EXECUTIVE_LOG(INFO) << LOG_BADGE("Sharding") << "ShardingTransactionExecutive call local"
                        << input->toFullString();
    return TransactionExecutive::externalCall(std::move(input));
}

std::string ShardingTransactionExecutive::getContractShard(const std::string_view& contractAddress)
{
    auto tableName = getContractTableName(contractAddress, m_blockContext.lock()->isWasm());
    return ContractShardUtils::getContractShard(storage(), tableName);
}