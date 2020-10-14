#include "EventLogFilter.h"
#include <json/json.h>
#include <libethcore/CommonJS.h>
using namespace dev;
using namespace dev::eth;
using namespace dev::event;

void EventLogFilter::matches(Block const& _block, Json::Value& _value)
{
    const auto& receipts = _block.transactionReceipts();
    const auto& transactions = _block.transactions();

    for (size_t i = 0; i < receipts->size(); ++i)
    {
        auto receipt = (*receipts)[i];
        auto tx = (*transactions)[i];
        auto logs = receipt->log();

        for (size_t j = 0; j < logs.size(); ++j)
        {
            auto const& log = logs[j];
            if (!matches(log))
            {
                continue;
            }

            Json::Value resp;
            resp["blockNumber"] = toString(_block.blockHeader().number());
            resp["blockHash"] = toJS(_block.blockHeader().hash());
            resp["address"] = toJS(log.address);
            resp["logIndex"] = toJS(j);
            resp["data"] = toJS(log.data);
            resp["topics"] = Json::Value(Json::arrayValue);

            for (std::size_t k = 0; k < log.topics.size(); ++k)
            {
                resp["topics"].append(toJS(log.topics[k]));
            }

            resp["transactionHash"] = toJS(tx->hash());
            resp["transactionIndex"] = toJS(i);

            _value.append(resp);
        }
    }
}

// filter individual log to see if the requirements are meet
bool EventLogFilter::matches(LogEntry const& _log)
{
    auto addresses = getParams()->getAddresses();
    auto topics = getParams()->getTopics();
    // An empty address array matches all values otherwise log.address must be in addresses
    if (!addresses.empty() && !addresses.count(_log.address))
        return false;

    bool isMatch = true;
    for (unsigned i = 0; i < eth::MAX_NUM_TOPIC_EVENT_LOG; ++i)
    {
        // The corresponding topic must be the same
        if (!topics[i].empty() && (_log.topics.size() < i || !topics[i].count(_log.topics[i])))
        {
            isMatch = false;
            break;
        }
    }

    return isMatch;
}
