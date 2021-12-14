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

#pragma once
#include <json/json.h>
#include <algorithm>
#include <memory>

namespace bcos
{
namespace ws
{
enum EnumPV : uint16_t
{
    None = 0,
    v1 = 1,
    // Focus: update current when websocket protocol upgrade
    CurrentVersion = v1
};

class ProtocolVersion
{
public:
    using Ptr = std::shared_ptr<ProtocolVersion>;
    using ConstPtr = std::shared_ptr<const ProtocolVersion>;

public:
    bool fromJson(const std::string& _json);
    Json::Value toJson();
    std::string toJsonString();

public:
    int protocolVersion() const { return m_protocolVersion; }
    void setProtocolVersion(int _protocolVersion) { m_protocolVersion = _protocolVersion; }

private:
    int m_protocolVersion;
};

}  // namespace ws
}  // namespace bcos