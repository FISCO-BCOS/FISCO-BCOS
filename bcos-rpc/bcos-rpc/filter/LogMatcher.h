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
    uint32_t matches(FilterRequest::ConstPtr _params, bcos::crypto::HashType&& _blockHash,
        const bcos::protocol::TransactionReceipt& _receipt, bcos::crypto::HashType&& _txHash,
        std::size_t _txIndex, Json::Value& _result, std::size_t _maxLogs, bool& _budgetExceeded);

    /// Outcome of matching one block.
    ///
    /// `budgetExceeded` means matching stopped BEFORE appending the log that would have crossed
    /// the caller's budget, so @p _result holds exactly `maxLogs` entries rather than the block's
    /// full match set. That is what makes the cap a bound on allocation and not merely on the
    /// successful response: a dense block can produce tens of thousands of matches, and appending
    /// them all before checking the total would leave the array unbounded for one block.
    struct BlockMatchOutcome
    {
        uint32_t matched = 0;
        bool budgetExceeded = false;
    };

    /// @param _maxLogs maximum number of entries this call may append to @p _result, counted
    ///        against the array's TOTAL size (so a caller accumulating across blocks passes the
    ///        per-query budget, not a per-block one). 0 means unlimited, which is what the
    ///        streaming paths use: they page by block and carry a cursor, so there is no
    ///        response ceiling to enforce and a rejection would stall the caller forever.
    BlockMatchOutcome matches(FilterRequest::ConstPtr _params,
        bcos::protocol::Block::ConstPtr _block, Json::Value& _result, std::size_t _maxLogs = 0);
};

}  // namespace bcos::rpc
