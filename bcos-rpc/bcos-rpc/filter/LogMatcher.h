#pragma once
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-rpc/filter/FilterRequest.h>
#include <json/json.h>


namespace bcos::rpc
{
class LogMatcher
{
public:
    using Ptr = std::shared_ptr<LogMatcher>;
    using ConstPtr = std::shared_ptr<const LogMatcher>;
    ~LogMatcher() {}

    bool matches(FilterRequest::ConstPtr _params, const bcos::protocol::LogEntry& _logEntry);

    uint32_t matches(FilterRequest::ConstPtr _params, bcos::crypto::HashType&& _blockHash,
        const bcos::protocol::TransactionReceipt& _receipt, bcos::crypto::HashType&& _txHash,
        std::size_t _txIndex, Json::Value& _result);

    uint32_t matches(FilterRequest::ConstPtr _params, bcos::protocol::Block::ConstPtr _block,
        Json::Value& _result);
};

}  // namespace bcos::rpc
