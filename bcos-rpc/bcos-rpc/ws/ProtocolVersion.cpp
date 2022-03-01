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
 * @file ProtocolVersion.h
 * @author: octopus
 * @date 2021-10-26
 */

#include <bcos-rpc/ws/ProtocolVersion.h>
#include <bcos-utilities/Log.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::ws;

bool ProtocolVersion::fromJson(const std::string& _json)
{
    try
    {
        Json::Value root;
        Json::Reader jsonReader;
        std::string errorMessage;
        do
        {
            if (!jsonReader.parse(_json, root))
            {
                errorMessage = "invalid json object";
                break;
            }

            if (!root.isMember("protocolVersion"))
            {  // id field not exist
                errorMessage = "Cannot find \'protocolVersion\' field";
                break;
            }

            int protocolVersion = root["protocolVersion"].asInt();
            setProtocolVersion(protocolVersion);

            BCOS_LOG(INFO) << LOG_BADGE("fromJson") << LOG_DESC("parser protocol version")
                           << LOG_KV("protocolVersion", protocolVersion);

            return true;
        } while (0);

        BCOS_LOG(ERROR) << LOG_BADGE("fromJson") << LOG_DESC("invalid protocol version json string")
                        << LOG_KV("json", _json) << LOG_KV("error", errorMessage);
    }
    catch (const std::exception& e)
    {
        BCOS_LOG(ERROR) << LOG_BADGE("fromJson") << LOG_DESC("invalid protocol version json string")
                        << LOG_KV("json", _json) << LOG_KV("exception", e.what());
    }

    return false;
}

Json::Value ProtocolVersion::toJson()
{
    /*
       {
       "protocolVersion": 1
       }
   */
    Json::Value jResult;
    // protocolVersion
    jResult["protocolVersion"] = m_protocolVersion;

    return jResult;
}

std::string ProtocolVersion::toJsonString()
{
    Json::Value jResult = toJson();

    Json::FastWriter writer;
    std::string result = writer.write(jResult);
    BCOS_LOG(ERROR) << LOG_BADGE("toJson") << LOG_DESC("generator protocol version string")
                    << LOG_KV("json", result);
    return result;
}
