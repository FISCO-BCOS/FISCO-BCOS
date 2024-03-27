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
 * @file Web3Endpoint.cpp
 * @author: kyonGuo
 * @date 2024/3/21
 */

#include "Web3Endpoint.h"

#include <bcos-rpc/Common.h>

using namespace bcos::rpc;

Web3Endpoint::Web3Endpoint(bcos::rpc::GroupManager::Ptr groupManager)
  : m_groupManager(std::move(groupManager))
{
    initMethod();
}

void Web3Endpoint::initMethod()
{
    // clang-format off
    m_methods[methodString(EthMethod::web3_clientVersion)] = std::bind(&Web3Endpoint::clientVersionInterface, this, std::placeholders::_1, std::placeholders::_2);
    m_methods[methodString(EthMethod::web3_sha3)] = std::bind(&Web3Endpoint::sha3Interface, this, std::placeholders::_1, std::placeholders::_2);
    // clang-format on
}

void Web3Endpoint::clientVersion(RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
void Web3Endpoint::sha3(RespFunc func)
{
    Json::Value result;
    func(BCOS_ERROR_PTR(MethodNotFound, "This API has not been implemented yet!"), result);
}
