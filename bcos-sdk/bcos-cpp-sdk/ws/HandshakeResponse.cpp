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
 * @file HandshakeResponse.h
 * @author: octopus
 * @date 2021-10-26
 */

#include <bcos-cpp-sdk/ws/Common.h>
#include <bcos-cpp-sdk/ws/HandshakeResponse.h>
#include <json/json.h>
#include <json/value.h>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::service;

bool HandshakeResponse::decode(std::string const& _data)
{
    try
    {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(_data, root))
        {
            RPC_WS_LOG(WARNING) << LOG_BADGE("HandshakeResponse decode: invalid json object")
                                << LOG_KV("data", _data);
            return false;
        }

        if (!root.isMember("protocolVersion"))
        {
            // id field not exist
            RPC_WS_LOG(WARNING) << LOG_BADGE(
                                       "HandshakeResponse decode: invalid for empty "
                                       "protocolVersion field")
                                << LOG_KV("data", _data);
            return false;
        }
        // set protocolVersion
        m_protocolVersion = root["protocolVersion"].asInt();

        if (root.isMember("groupInfoList") && root["groupInfoList"].isArray())
        {
            auto& jGroupInfoList = root["groupInfoList"];
            for (Json::ArrayIndex i = 0; i < jGroupInfoList.size(); ++i)
            {
                Json::FastWriter writer;
                std::string str = writer.write(jGroupInfoList[i]);
                auto groupInfo = m_groupInfoCodec->deserialize(str);

                RPC_WS_LOG(INFO) << LOG_BADGE("fromJson") << LOG_DESC(" new group info")
                                 << LOG_KV("groupInfo", printGroupInfo(groupInfo));

                m_groupInfoList.push_back(groupInfo);
            }
        }

        // "groupBlockNumber": [{"group0": 1}, {"group1": 2}, {"group2": 3}]
        if (root.isMember("groupBlockNumber") && root["groupBlockNumber"].isArray())
        {
            for (Json::ArrayIndex i = 0; i < root["groupBlockNumber"].size(); ++i)
            {
                Json::Value jGroupBlockNumber = root["groupBlockNumber"][i];
                for (auto const& groupID : jGroupBlockNumber.getMemberNames())
                {
                    int64_t blockNumber = jGroupBlockNumber[groupID].asInt64();

                    m_groupBlockNumber[groupID] = blockNumber;
                }
            }
        }

        RPC_WS_LOG(INFO) << LOG_BADGE("fromJson") << LOG_DESC("parser protocol version")
                         << LOG_KV("protocolVersion", m_protocolVersion)
                         << LOG_KV("groupInfoList size", m_groupInfoList.size())
                         << LOG_KV("groupBlockNumber size", m_groupBlockNumber.size());

        return true;
    }
    catch (const std::exception& e)
    {
        RPC_WS_LOG(WARNING) << LOG_BADGE("fromJson")
                            << LOG_DESC("invalid protocol version json string")
                            << LOG_KV("data", _data)
                            << LOG_KV("exception", boost::diagnostic_information(e));
    }
    return false;
}

void HandshakeResponse::encode(std::string& _encodedData) const
{
    Json::Value encodedJson;

    encodedJson["protocolVersion"] = m_protocolVersion;
    encodedJson["groupInfoList"] = Json::Value(Json::arrayValue);
    for (const auto& groupInfo : m_groupInfoList)
    {
        auto groupInfoResponse = m_groupInfoCodec->serialize(groupInfo);
        encodedJson["groupInfoList"].append(groupInfoResponse);
    }
    Json::FastWriter writer;
    _encodedData = writer.write(encodedJson);
}