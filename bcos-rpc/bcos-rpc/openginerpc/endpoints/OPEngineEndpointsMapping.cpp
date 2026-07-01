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
 * @file OPEngineEndpointsMapping.cpp
 * @date 2026/5/20
 */

#include "OPEngineEndpointsMapping.h"
#include "OPEngineMethods.h"

namespace bcos::rpc
{
std::optional<OPEngineEndpointsMapping::Handler> OPEngineEndpointsMapping::findHandler(
    const std::string& _method) const
{
    auto it = m_handlers.find(_method);
    if (it == m_handlers.end())
    {
        return std::nullopt;
    }
    return it->second;
}

void OPEngineEndpointsMapping::addHandlers()
{
    addEngineHandlers();
}

void OPEngineEndpointsMapping::addEngineHandlers()
{
    m_handlers[methodString(OPEngineMethod::engine_exchangeCapabilities)] =
        &OPEngineEndpoints::exchangeCapabilities;
    m_handlers[methodString(OPEngineMethod::engine_forkchoiceUpdatedV1)] =
        &OPEngineEndpoints::forkchoiceUpdatedV1;
    m_handlers[methodString(OPEngineMethod::engine_forkchoiceUpdatedV2)] =
        &OPEngineEndpoints::forkchoiceUpdatedV2;
    m_handlers[methodString(OPEngineMethod::engine_forkchoiceUpdatedV3)] =
        &OPEngineEndpoints::forkchoiceUpdatedV3;
    m_handlers[methodString(OPEngineMethod::engine_forkchoiceUpdatedV4)] =
        &OPEngineEndpoints::forkchoiceUpdatedV4;
    m_handlers[methodString(OPEngineMethod::engine_getPayloadV1)] =
        &OPEngineEndpoints::getPayloadV1;
    m_handlers[methodString(OPEngineMethod::engine_getPayloadV2)] =
        &OPEngineEndpoints::getPayloadV2;
    m_handlers[methodString(OPEngineMethod::engine_getPayloadV3)] =
        &OPEngineEndpoints::getPayloadV3;
    m_handlers[methodString(OPEngineMethod::engine_getPayloadV4)] =
        &OPEngineEndpoints::getPayloadV4;
    m_handlers[methodString(OPEngineMethod::engine_newPayloadV1)] =
        &OPEngineEndpoints::newPayloadV1;
    m_handlers[methodString(OPEngineMethod::engine_newPayloadV2)] =
        &OPEngineEndpoints::newPayloadV2;
    m_handlers[methodString(OPEngineMethod::engine_newPayloadV3)] =
        &OPEngineEndpoints::newPayloadV3;
    m_handlers[methodString(OPEngineMethod::engine_newPayloadV4)] =
        &OPEngineEndpoints::newPayloadV4;
}
}  // namespace bcos::rpc
