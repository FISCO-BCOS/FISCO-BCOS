#include <bcos-rpc/filter/LogMatcher.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/DataConvertUtility.h>

using namespace bcos;
using namespace bcos::rpc;

uint32_t LogMatcher::matches(
    FilterRequest::ConstPtr _params, bcos::protocol::Block::ConstPtr _block, Json::Value& _result)
{
    uint32_t count = 0;
    for (std::size_t index = 0; index < _block->transactionsMetaDataSize(); index++)
    {
        count += matches(_params, _block->blockHeaderConst()->hash(), _block->receipt(index),
            _block->transactionHash(index), index, _result);
    }

    return count;
}

uint32_t LogMatcher::matches(FilterRequest::ConstPtr _params, bcos::crypto::HashType&& _blockHash,
    bcos::protocol::TransactionReceipt::ConstPtr&& _receipt, bcos::crypto::HashType&& _txHash,
    std::size_t _txIndex, Json::Value& _result)
{
    uint32_t count = 0;
    auto blockNumber = _receipt->blockNumber();
    auto mutableReceipt = const_cast<bcos::protocol::TransactionReceipt*>(_receipt.get());
    auto logEntries = mutableReceipt->takeLogEntries();
    for (size_t i = 0; i < logEntries.size(); i++)
    {
        const auto& logEntry = logEntries[i];
        if (matches(_params, logEntry))
        {
            count++;
            Json::Value log;
            log["data"] = toHexStringWithPrefix(logEntry.data());
            log["logIndex"] = toQuantity(i);
            log["blockNumber"] = toQuantity(blockNumber);
            log["blockHash"] = _blockHash.hexPrefixed();
            log["transactionIndex"] = toQuantity(_txIndex);
            log["transactionHash"] = _txHash.hexPrefixed();
            log["removed"] = false;
            log["address"] = "0x" + std::string(logEntry.address());
            Json::Value jTopics(Json::arrayValue);
            for (const auto& topic : logEntry.topics())
            {
                jTopics.append(topic.hexPrefixed());
            }
            log["topics"] = std::move(jTopics);
            _result.append(std::move(log));
        }
    }
    return count;
}

bool LogMatcher::matches(FilterRequest::ConstPtr _params, const bcos::protocol::LogEntry& _logEntry)
{
    const auto& addresses = _params->addresses();
    const auto& topics = _params->topics();
    const auto& logTopics = _logEntry.topics();

    FILTER_LOG(TRACE) << LOG_BADGE("matches") << LOG_KV("address", _logEntry.address())
                      << LOG_KV("logEntry topics", _logEntry.topics().size());

    // An empty address array matches all values otherwise log.address must be in addresses
    if (!addresses.empty() && !addresses.count("0x" + std::string(_logEntry.address())))
    {
        return false;
    }

    if (topics.size() > logTopics.size())
    {
        return false;
    }

    for (size_t i = 0; i < topics.size(); ++i)
    {
        const auto& sub = topics[i];
        if (sub.empty())
        {
            continue;
        }
        if (!sub.contains(logTopics[i].hexPrefixed()))
        {
            return false;
        }
    }
    return true;
}
