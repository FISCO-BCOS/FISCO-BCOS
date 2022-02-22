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
 * @file EvenPushResponse.h
 * @author: octopus
 * @date 2021-09-09
 */

#pragma once

#include <json/value.h>
#include <memory>
#include <string>
namespace bcos
{
namespace event
{
class EventSubResponse
{
public:
    using Ptr = std::shared_ptr<EventSubResponse>;

public:
    std::string id() const { return m_id; }
    void setId(const std::string& _id) { m_id = _id; }
    int status() const { return m_status; }
    void setStatus(int _status) { m_status = _status; }

    void setJResp(const Json::Value& _jResp) { m_jResp = _jResp; }
    Json::Value jResp() const { return m_jResp; }

public:
    std::string generateJson();
    bool fromJson(const std::string& _response);

private:
    std::string m_id;
    int m_status;

    Json::Value m_jResp;
};
}  // namespace event
}  // namespace bcos