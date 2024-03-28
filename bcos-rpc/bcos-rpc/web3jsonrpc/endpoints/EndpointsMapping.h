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
 * @file EndpointsMapping.h
 * @author: kyonGuo
 * @date 2024/3/28
 */

#pragma once

#include "Endpoints.h"

#include <bcos-task/Task.h>
#include <json/json.h>

namespace bcos::rpc
{
class EndpointsMapping
{
public:
    using Handler = task::Task<void> (Endpoints::*)(const Json::Value&, Json::Value&);
    EndpointsMapping() { addHandlers(); };
    ~EndpointsMapping() = default;
    EndpointsMapping(const EndpointsMapping&) = delete;
    EndpointsMapping& operator=(const EndpointsMapping&) = delete;

    [[nodiscard]] std::optional<Handler> findHandler(const std::string& _method) const;

private:
    void addHandlers();
    void addEthHandlers();
    void addNetHandlers();
    void addWeb3Handlers();

    std::unordered_map<std::string, Handler> m_handlers;
};
}  // namespace bcos::rpc
