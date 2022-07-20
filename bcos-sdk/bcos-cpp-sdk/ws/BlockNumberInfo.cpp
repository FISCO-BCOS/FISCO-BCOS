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
 * @file BlockNumberInfo.cpp
 * @author: octopus
 * @date 2021-10-04
 */

#include <bcos-cpp-sdk/ws/BlockNumberInfo.h>
#include <bcos-cpp-sdk/ws/Common.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::service;

std::string BlockNumberInfo::toJson()
{
    // eg: {"blockNumber": 11, "group": "group", "nodeName": "node"}

    Json::Value jValue;
    jValue["group"] = m_group;
    jValue["blockNumber"] = m_blockNumber;
    jValue["nodeName"] = m_blockNumber;

    Json::FastWriter writer;
    std::string s = writer.write(jValue);
    return s;
}

bool BlockNumberInfo::fromJson(const std::string& _json)
{
    std::string errorMessage;
    try
    {
        std::string group;
        std::string node;
        int64_t blockNumber = -1;
        do
        {
            Json::Value root;
            Json::Reader jsonReader;
            if (!jsonReader.parse(_json, root))
            {
                errorMessage = "invalid json object";
                break;
            }

            if (!root.isMember("blockNumber"))
            {
                errorMessage = "request has no blockNumber field";
                break;
            }
            blockNumber = root["blockNumber"].asInt64();

            if (!root.isMember("group"))
            {
                errorMessage = "request has no group field";
                break;
            }
            group = root["group"].asString();

            if (!root.isMember("nodeName"))
            {
                errorMessage = "request has no nodeName field";
                break;
            }
            node = root["nodeName"].asString();

            m_blockNumber = blockNumber;
            m_group = group;
            m_node = node;

            RPC_BLOCKNUM_LOG(INFO)
                << LOG_BADGE("fromJson") << LOG_KV("group", m_group) << LOG_KV("node", m_node)
                << LOG_KV("blockNumber", m_blockNumber);

            return true;

        } while (0);

        RPC_BLOCKNUM_LOG(WARNING) << LOG_BADGE("fromJson") << LOG_DESC("Invalid JSON")
                                  << LOG_KV("errorMessage", errorMessage);
    }
    catch (const std::exception& e)
    {
        RPC_BLOCKNUM_LOG(WARNING) << LOG_BADGE("fromJson") << LOG_DESC("Invalid JSON")
                                  << LOG_KV("error", boost::diagnostic_information(e));
    }

    return false;
}