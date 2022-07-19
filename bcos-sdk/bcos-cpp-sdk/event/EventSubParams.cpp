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
 * @file EvenPushParams.cpp
 * @author: octopus
 * @date 2021-09-01
 */

#include <bcos-cpp-sdk/event/EventSubParams.h>
#include <bcos-cpp-sdk/utilities/abi/ContractEventTopic.h>
#include <json/json.h>
#include <exception>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::event;

bool EventSubParams::verifyParams()
{
    // topic
    if (m_topics.size() > EVENT_LOG_TOPICS_MAX_INDEX)
    {
        return false;
    }

    for (std::size_t i = 0; i < m_topics.size(); ++i)
    {
        for (const auto& topic : m_topics[i])
        {
            if (!codec::abi::ContractEventTopic::validEventTopic(topic))
            {
                return false;
            }
        }
    }

    // check address
    for (const auto& addr : m_addresses)
    {
        if (addr.empty())
        {
            return false;
        }
    }

    // from to range check
    if (m_fromBlock > 0 && m_toBlock > 0)
    {
        return m_fromBlock <= m_toBlock;
    }

    return true;
}

bool EventSubParams::fromJsonString(const std::string& _jsonString)
{
    Json::Value root;
    Json::Reader jsonReader;

    try
    {
        if (!jsonReader.parse(_jsonString, root))
        {
            EVENT_PARAMS(WARNING) << LOG_BADGE("fromJsonString") << LOG_DESC("invalid json object")
                                  << LOG_KV("jsonString", _jsonString);
            return false;
        }

        fromJson(root);

        EVENT_PARAMS(INFO) << LOG_BADGE("fromJsonString") << LOG_KV("jsonString", _jsonString)
                           << LOG_KV("params", *this);
        return true;
    }
    catch (const std::exception& _e)
    {
        EVENT_PARAMS(WARNING) << LOG_BADGE("fromJsonString")
                              << LOG_DESC("invalid event sub params json object")
                              << LOG_KV("jsonString", _jsonString)
                              << LOG_KV("error", boost::diagnostic_information(_e));
        return false;
    }
}

void EventSubParams::fromJson(const Json::Value& jParams)
{
    if (jParams.isMember("fromBlock"))
    {
        setFromBlock(jParams["fromBlock"].asInt64());
    }

    if (jParams.isMember("toBlock"))
    {
        setToBlock(jParams["toBlock"].asInt64());
    }

    if (jParams.isMember("addresses"))
    {
        auto& jAddr = jParams["addresses"];
        for (Json::Value::ArrayIndex index = 0; index < jAddr.size(); ++index)
        {
            addAddress(jAddr[index].asString());
        }
    }

    if (jParams.isMember("topics"))
    {
        auto& jTopics = jParams["topics"];
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
                    addTopic(index, jIndex[innerIndex].asString());
                }
            }
            else
            {  // single topic, string value
                addTopic(index, jIndex.asString());
            }
        }
    }

    EVENT_PARAMS(DEBUG) << LOG_BADGE("fromJson") << LOG_KV("EventSubParams", *this);
}

std::string EventSubParams::toJsonString()
{
    Json::FastWriter writer;
    std::string result = writer.write(toJson());
    return result;
}

Json::Value EventSubParams::toJson()
{
    Json::Value jParams;
    // fromBlock
    jParams["fromBlock"] = fromBlock();
    // toBlock
    jParams["toBlock"] = toBlock();

    // addresses
    Json::Value jAddresses(Json::arrayValue);
    for (const auto& addr : addresses())
    {
        jAddresses.append(addr);
    }
    jParams["addresses"] = jAddresses;

    // topics
    Json::Value jTopics(Json::arrayValue);
    for (const auto& inTopics : topics())
    {
        if (inTopics.empty())
        {
            Json::Value jInTopics(Json::nullValue);
            jTopics.append(jInTopics);
            continue;
        }

        Json::Value jInTopics(Json::arrayValue);
        for (const auto& topic : inTopics)
        {
            jInTopics.append(topic);
        }
        jTopics.append(jInTopics);
    }

    jParams["topics"] = jTopics;
    return jParams;
}