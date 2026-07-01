/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file OPEngineEndpointsMapping.h
 * @date 2026/5/20
 */

#pragma once

#include "OPEngineEndpoints.h"
#include <bcos-task/Task.h>
#include <json/json.h>
#include <optional>
#include <string>
#include <unordered_map>

namespace bcos::rpc
{
class OPEngineEndpointsMapping
{
public:
    using Handler = task::Task<Json::Value> (OPEngineEndpoints::*)(const Json::Value&);

    OPEngineEndpointsMapping() { addHandlers(); }
    ~OPEngineEndpointsMapping() = default;
    OPEngineEndpointsMapping(const OPEngineEndpointsMapping&) = delete;
    OPEngineEndpointsMapping& operator=(const OPEngineEndpointsMapping&) = delete;

    [[nodiscard]] std::optional<Handler> findHandler(const std::string& _method) const;

private:
    void addHandlers();
    void addEngineHandlers();

    std::unordered_map<std::string, Handler> m_handlers;
};
}  // namespace bcos::rpc
