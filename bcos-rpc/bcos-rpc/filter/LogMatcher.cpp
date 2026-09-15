#include <bcos-protocol/TransactionStatus.h>
#include <bcos-rlp-protocol/BlockHeaderHash.h>
#include <bcos-rpc/filter/Common.h>
#include <bcos-rpc/filter/LogMatcher.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/DataConvertUtility.h>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::protocol;

uint32_t LogMatcher::matches(
    FilterRequest::ConstPtr _params, bcos::protocol::Block::ConstPtr _block, Json::Value& _result)
{
    uint32_t count = 0;
    auto receipts = _block->receipts();
    auto const blockHash = bcos::protocol::canonicalBlockHash(*_block->blockHeader());
    for (std::size_t index = 0; index < _block->transactionsMetaDataSize(); index++)
    {
        auto receipt = receipts[index];
        count += matches(_params, crypto::HashType(blockHash), *receipt,
            _block->transactionHash(index), index, _result);
    }

    return count;
}

uint32_t LogMatcher::matches(FilterRequest::ConstPtr _params, bcos::crypto::HashType&& _blockHash,
    const bcos::protocol::TransactionReceipt& _receipt, bcos::crypto::HashType&& _txHash,
    std::size_t _txIndex, Json::Value& _result)
{
    uint32_t count = 0;
    auto blockNumber = _receipt.blockNumber();
    if (_receipt.status() != int32_t(TransactionStatus::None))
    {
        return 0;
    }
    auto logEntries = _receipt.logEntries();
    for (size_t i = 0; i < logEntries.size(); i++)
    {
        const auto& logEntry = logEntries[i];
        if (matches(_params, logEntry))
        {
            count++;
            Json::Value log;
            log["data"] = toHexStringWithPrefix(logEntry.data());
            // block-wide index, same base as ReceiptResponse.cpp (issue #5553)
            log["logIndex"] = toQuantity(_receipt.logIndex() + i);
            log["blockNumber"] = toQuantity(blockNumber);
            log["blockHash"] = _blockHash.hexPrefixed();
            log["transactionIndex"] = toQuantity(_txIndex);
            log["transactionHash"] = _txHash.hexPrefixed();
            log["removed"] = false;
            // Same lane-dependent address form as the receipt encoder: normalize through
            // the shared helper so eth_getLogs and eth_getTransactionReceipt agree.
            log["address"] = "0x" + logEntryAddressHex(logEntry);
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

    // An empty address array matches all values otherwise log.address must be in addresses.
    // Normalize through logEntryAddressHex like every other consumer: the OP lane stores the
    // raw 20 bytes in LogEntry::address, so concatenating "0x" with the raw form can never
    // equal a requested hex address and every address-filtered query would match nothing.
    if (!addresses.empty() && !addresses.count("0x" + logEntryAddressHex(_logEntry)))
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
