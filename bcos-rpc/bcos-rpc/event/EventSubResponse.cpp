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
 * @file EvenPushResponse.cpp
 * @author: octopus
 * @date 2021-09-09
 */
#include <bcos-rpc/event/Common.h>
#include <bcos-rpc/event/EventSubResponse.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::event;

std::string EventSubResponse::generateJson()
{
    /*
        {
        "id": "0x123",
        "status": 0,
        "result": {
            "blockNumber": 111,
            "events":[]
        }
        }
    */
    Json::Value jResult;
    // id
    jResult["id"] = m_id;
    // status
    jResult["status"] = m_status;

    m_jResp = jResult;

    Json::FastWriter writer;
    std::string result = writer.write(jResult);
    return result;
}

bool EventSubResponse::fromJson(const std::string& _response)
{
    std::string id;
    int status;

    try
    {
        Json::Value root;
        Json::Reader jsonReader;
        std::string errorMessage;
        do
        {
            if (!jsonReader.parse(_response, root))
            {
                errorMessage = "invalid json object, parse response failed";
                break;
            }

            if (!root.isMember("id"))
            {  // id field not exist
                errorMessage = "\'id\' field not exist";
                break;
            }
            id = root["id"].asString();

            if (!root.isMember("status"))
            {
                // group field not exist
                errorMessage = "\'status\' field not exist";
                break;
            }
            status = root["status"].asInt();

            m_id = id;
            m_status = status;
            m_jResp = root;

            EVENT_RESPONSE(TRACE) << LOG_BADGE("fromJson")
                                  << LOG_DESC("parse event sub response success")
                                  << LOG_KV("id", m_id) << LOG_KV("status", m_status);

            return true;

        } while (0);

        EVENT_RESPONSE(ERROR) << LOG_BADGE("fromJson") << LOG_DESC("invalid event sub response")
                              << LOG_KV("response", _response) << LOG_KV("error", errorMessage);
    }
    catch (const std::exception& e)
    {
        EVENT_RESPONSE(ERROR) << LOG_BADGE("fromJson") << LOG_DESC("invalid json object")
                              << LOG_KV("response", _response)
                              << LOG_KV("error", std::string(e.what()));
    }

    return false;
}
