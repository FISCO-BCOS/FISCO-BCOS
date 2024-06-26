#pragma once

#include <bcos-framework/Common.h>
#include <bcos-rpc/Common.h>

// The largest number of topic in one event log
#define EVENT_LOG_TOPICS_MAX_INDEX (4)

#define FILTER_LOG(LEVEL) BCOS_LOG(LEVEL) << "[FILTER]"

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

}  // namespace rpc
}  // namespace bcos
