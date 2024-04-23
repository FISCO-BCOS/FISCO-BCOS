/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file FilterRequest.cpp
 * @author: kyonGuo
 * @date 2024/4/11
 */
#include <bcos-rpc/filter/FilterRequest.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/util.h>
#include <boost/exception/diagnostic_information.hpp>

using namespace bcos;
using namespace bcos::rpc;

static inline std::string removePrefix(const std::string& str)
{
    if ((str.compare(0, 2, "0x") == 0) || (str.compare(0, 2, "0X") == 0))
    {
        return str.substr(2);
    }
    return str;
}

void FilterRequest::fromJson(const Json::Value& jParams, protocol::BlockNumber latest)
{
    // default: LatestBlockNumber
    m_fromBlock = latest;
    if (jParams.isMember("fromBlock") && !jParams["fromBlock"].isNull())
    {
        auto [blockNumber, isLatest] = getBlockNumberByTag(latest, jParams["fromBlock"].asString());
        m_fromBlock = blockNumber;
        m_fromIsLatest = isLatest;
    }

    // default: LatestBlockNumber
    m_toBlock = latest;
    if (jParams.isMember("toBlock") && !jParams["toBlock"].isNull())
    {
        auto [blockNumber, isLatest] = getBlockNumberByTag(latest, jParams["toBlock"].asString());
        m_toBlock = blockNumber;
        m_toIsLatest = isLatest;
    }

    if (jParams.isMember("address") && !jParams["address"].isNull())
    {
        auto& jAddresses = jParams["address"];
        if (jAddresses.isArray())
        {
            for (Json::Value::ArrayIndex index = 0; index < jAddresses.size(); ++index)
            {
                addAddress(removePrefix(jAddresses[index].asString()));
            }
        }
        else
        {
            addAddress(removePrefix(jAddresses.asString()));
        }
    }

    if (jParams.isMember("topics") && !jParams["topics"].isNull())
    {
        auto& jTopics = jParams["topics"];

        // check number of topic
        if (jTopics.size() > EVENT_LOG_TOPICS_MAX_INDEX)
        {
            FILTER_LOG(ERROR) << LOG_BADGE("fromJson") << LOG_DESC("exceed max topics")
                              << LOG_KV("topicsSize", jTopics.size());
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParamsCode(), "exceed max topics"));
        }
        resizeTopic(jTopics.size());
        for (Json::Value::ArrayIndex index = 0; index < jTopics.size(); ++index)
        {
            auto& jIndex = jTopics[index];
            if (jIndex.isNull())
            {
                continue;
            }

            if (jIndex.isArray())
            {  // array topics
                for (Json::Value::ArrayIndex innerIndex = 0; innerIndex < jIndex.size();
                     ++innerIndex)
                {
                    addTopic(index, removePrefix(jIndex[innerIndex].asString()));
                }
            }
            else
            {  // single topic, string value
                addTopic(index, removePrefix(jIndex.asString()));
            }
        }
    }

    if (jParams.isMember("blockhash") && !jParams["blockhash"].isNull())
    {
        m_blockHash = jParams["blockhash"].asString();
    }
}

bool FilterRequest::checkBlockRange()
{
    if (fromBlock() < 0 || toBlock() < 0)
    {
        return false;
    }
    if (fromIsLatest() && toIsLatest())
    {
        return true;
    }
    if (!fromIsLatest() && !toIsLatest() && toBlock() >= fromBlock())
    {
        return true;
    }
    if (!fromIsLatest() && toIsLatest())
    {
        return true;
    }
    return false;
}