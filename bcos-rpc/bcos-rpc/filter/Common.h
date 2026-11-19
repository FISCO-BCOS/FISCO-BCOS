#pragma once

#include "bcos-framework/Common.h"
#include <bcos-utilities/LogStream.h>

#include <cstddef>

// The largest number of topic in one event log
#define EVENT_LOG_TOPICS_MAX_INDEX (4)

#define FILTER_LOG(LEVEL) BCOS_MODULE_LOG(RPC, LEVEL) << "[FILTER]"

namespace bcos
{
namespace rpc
{

enum SubscriptionType
{
    // LogsSubscription queries for new
    LogsSubscription = 0,
    // PendingTransactionsSubscription queries for pending transactions entering the pending state
    PendingTransactionsSubscription,
    // BlocksSubscription queries hashes for blocks that are imported
    BlocksSubscription,
    // LastIndexSubscription keeps track of the last index
    LastIndexSubscription
};

// Trigger a filter cleanup operation every 3s
static constexpr const uint64_t CLEANUP_FILTER_TIME = 3000;
static constexpr const uint64_t MAX_TRAVERSE_FILTERS_COUNT = 10000;
// the filter expiration time, default is 5 minutes
static constexpr const uint64_t FILTER_DEFAULT_EXPIRATION_TIME = uint64_t(60 * 5 * 1000);

// Hard ceiling on the number of logs one one-shot query may return. The block-range cap alone
// bounds how many blocks are read, not how much comes back: a range inside the cap can still
// match an unbounded number of logs (block size is now gas-limited only, so a single block can
// carry thousands of transactions), and the response is built as one in-memory Json::Value.
// Exceeding it is an error rather than a truncation, for the same reason the range cap is: a
// prefix of the requested log set is a plausible-looking wrong answer.
static constexpr const size_t MAX_RETURNED_LOGS_PER_QUERY = 10000;

}  // namespace rpc
}  // namespace bcos
