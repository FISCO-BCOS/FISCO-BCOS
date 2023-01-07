//
// Created by Jimmy Shi on 2023/1/7.
//

#include "ShardingBlockExecutive.h"
#include "SchedulerImpl.h"

using namespace bcos::scheduler;
using namespace bcos::storage;


std::shared_ptr<DmcExecutor> ShardingBlockExecutive::registerAndGetDmcExecutor(
    std::string contractAddress)
{
    auto shardName = getContractShard(contractAddress);
    return BlockExecutive::registerAndGetDmcExecutor(shardName);
};

inline std::string getContractTableName(const std::string_view& _address)
{
    constexpr static std::string_view prefix("/apps/");
    std::string out;
    if (_address[0] == '/')
    {
        out.reserve(prefix.size() + _address.size() - 1);
        std::copy(prefix.begin(), prefix.end(), std::back_inserter(out));
        std::copy(_address.begin() + 1, _address.end(), std::back_inserter(out));
    }
    else
    {
        out.reserve(prefix.size() + _address.size());
        std::copy(prefix.begin(), prefix.end(), std::back_inserter(out));
        std::copy(_address.begin(), _address.end(), std::back_inserter(out));
    }

    return out;
}

std::string ShardingBlockExecutive::getContractShard(const std::string& contractAddress)
{
    if (!m_storageWrapper.has_value())
    {
        auto stateStorage = std::make_shared<StateStorage>(getStorage());
        auto recorder = std::make_shared<Recoder>();
        m_storageWrapper.emplace(stateStorage, recorder);
    }

    auto shard = m_contract2Shard.find(contractAddress);
    std::string shardName;

    if (shard == m_contract2Shard.end())
    {
        WriteGuard l(x_contract2Shard);
        shard = m_contract2Shard.find(contractAddress);
        if (shard == m_contract2Shard.end())
        {
            auto tableName = getContractTableName(contractAddress);
            shardName = ContractShardUtils::getContractShard(m_storageWrapper.value(), tableName);
            m_contract2Shard.emplace(contractAddress, shardName);
        }
        else
        {
            shardName = shard->second;
        }
    }

    return shardName;
}
