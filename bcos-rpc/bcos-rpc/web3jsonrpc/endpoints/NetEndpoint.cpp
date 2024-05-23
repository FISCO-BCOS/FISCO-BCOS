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
 * @file NetEndpoint.cpp
 * @author: kyonGuo
 * @date 2024/3/21
 */

#include "NetEndpoint.h"

#include <bcos-framework/ledger/Ledger.h>
#include <bcos-ledger/src/libledger/LedgerMethods.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>

using namespace bcos;
using namespace bcos::rpc;
task::Task<void> NetEndpoint::verison(const Json::Value&, Json::Value& response)
{
    auto const ledger = m_nodeService->ledger();
    auto config = co_await ledger::getSystemConfig(*ledger, ledger::SYSTEM_KEY_WEB3_CHAIN_ID);
    Json::Value result;
    if (config.has_value())
    {
        try
        {
            auto [chainId, _] = config.value();
            result = toQuantity(std::stoull(chainId));
        }
        catch (...)
        {
            result = "0x4ee8";  // 20200
        }
    }
    else
    {
        result = "0x4ee8";  // 20200
    }
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> NetEndpoint::listening(const Json::Value&, Json::Value& response)
{
    Json::Value result = true;
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> NetEndpoint::peerCount(const Json::Value&, Json::Value& response)
{
    auto const sync = m_nodeService->sync();
    auto const status = sync->getPeerStatus();
    Json::Value result = Json::UInt64(status.size());
    buildJsonContent(result, response);
    co_return;
}
