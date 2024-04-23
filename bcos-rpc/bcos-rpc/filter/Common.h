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

}  // namespace rpc
}  // namespace bcos
