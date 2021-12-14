/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file EvenPushMatcher.cpp
 * @author: octopus
 * @date 2021-09-10
 */

#include "libutilities/BoostLog.h"
#include <bcos-rpc/event/Common.h>
#include <bcos-rpc/event/EventSubMatcher.h>

using namespace bcos;
using namespace bcos::event;

uint32_t EventSubMatcher::matches(
    EventSubParams::ConstPtr _params, bcos::protocol::Block::ConstPtr _block, Json::Value& _result)
{
    uint32_t count = 0;
    for (std::size_t index = 0; index < _block->transactionsSize(); index++)
    {
        count +=
            matches(_params, _block->receipt(index), _block->transaction(index), index, _result);
    }

    return count;
}

uint32_t EventSubMatcher::matches(EventSubParams::ConstPtr _params,
    bcos::protocol::TransactionReceipt::ConstPtr _receipt,
    bcos::protocol::Transaction::ConstPtr _tx, std::size_t _txIndex, Json::Value& _result)
{
    uint32_t count = 0;
    const auto& logEntries = _receipt->logEntries();
    std::size_t logIndex = 0;
    for (const auto& logEntry : logEntries)
    {
        if (matches(_params, logEntry))
        {
            count++;

            Json::Value jResp;
            jResp["blockNumber"] = _receipt->blockNumber();
            jResp["address"] = std::string(logEntry.address());
            jResp["data"] = toHexStringWithPrefix(logEntry.data());
            jResp["logIndex"] = (uint64_t)logIndex;
            jResp["transactionHash"] = _tx->hash().hexPrefixed();
            jResp["transactionIndex"] = (uint64_t)_txIndex;
            jResp["topics"] = Json::Value(Json::arrayValue);
            for (const auto& topic : logEntry.topics())
            {
                jResp["topics"].append(topic.hexPrefixed());
            }
            _result.append(jResp);
        }

        logIndex += 1;
    }

    return count;
}

bool EventSubMatcher::matches(
    EventSubParams::ConstPtr _params, const bcos::protocol::LogEntry& _logEntry)
{
    const auto& addresses = _params->addresses();
    const auto& topics = _params->topics();

    // EVENT_MATCH(TRACE) << LOG_BADGE("matches") << LOG_KV("address", _logEntry.address())
    //                    << LOG_KV("logEntry topics", _logEntry.topics().size());

    // An empty address array matches all values otherwise log.address must be in addresses
    if (!addresses.empty() && !addresses.count(std::string(_logEntry.address())))
    {
        return false;
    }

    bool isMatch = true;
    for (unsigned i = 0; i < EVENT_LOG_TOPICS_MAX_INDEX; ++i)
    {
        const auto& logTopics = _logEntry.topics();
        if (topics.size() > i && !topics[i].empty() &&
            (logTopics.size() <= i || !topics[i].count(logTopics[i].hex())))
        {
            isMatch = false;
            break;
        }
    }

    return isMatch;
}
