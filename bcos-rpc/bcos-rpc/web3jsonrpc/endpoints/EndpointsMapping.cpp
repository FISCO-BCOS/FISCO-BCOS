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
 * @file EndpointsMapping.cpp
 * @author: kyonGuo
 * @date 2024/3/28
 */

#include "EndpointsMapping.h"

#include "EthMethods.h"

#include <bcos-rpc/jsonrpc/Common.h>

namespace bcos::rpc
{
std::optional<EndpointsMapping::Handler> EndpointsMapping::findHandler(
    const std::string& _method) const
{
    auto it = m_handlers.find(_method);
    if (it == m_handlers.end())
    {
        return std::nullopt;
    }
    return it->second;
}

void EndpointsMapping::addHandlers()
{
    addEthHandlers();
    addNetHandlers();
    addWeb3Handlers();
    for (auto& [method, _] : m_handlers)
    {
        WEB3_LOG(INFO) << LOG_BADGE("initHandler") << LOG_KV("method", method);
    }
    WEB3_LOG(INFO) << LOG_BADGE("initHandler") << LOG_KV("size", m_handlers.size());
}

void EndpointsMapping::addEthHandlers()
{
    // clang-format off
    m_handlers[methodString(EthMethod::eth_protocolVersion)] = &Endpoints::protocolVersion;
    m_handlers[methodString(EthMethod::eth_syncing)] = &Endpoints::syncing;
    m_handlers[methodString(EthMethod::eth_coinbase)] = &Endpoints::coinbase;
    m_handlers[methodString(EthMethod::eth_chainId)] = &Endpoints::chainId;
    m_handlers[methodString(EthMethod::eth_mining)] = &Endpoints::mining;
    m_handlers[methodString(EthMethod::eth_hashrate)] = &Endpoints::hashrate;
    m_handlers[methodString(EthMethod::eth_gasPrice)] = &Endpoints::gasPrice;
    m_handlers[methodString(EthMethod::eth_accounts)] = &Endpoints::accounts;
    m_handlers[methodString(EthMethod::eth_blockNumber)] = &Endpoints::blockNumber;
    m_handlers[methodString(EthMethod::eth_getBalance)] = &Endpoints::getBalance;
    m_handlers[methodString(EthMethod::eth_getStorageAt)] = &Endpoints::getStorageAt;
    m_handlers[methodString(EthMethod::eth_getTransactionCount)] = &Endpoints::getTransactionCount;
    m_handlers[methodString(EthMethod::eth_getBlockTransactionCountByHash)] = &Endpoints::getBlockTxCountByHash;
    m_handlers[methodString(EthMethod::eth_getBlockTransactionCountByNumber)] = &Endpoints::getBlockTxCountByNumber;
    m_handlers[methodString(EthMethod::eth_getUncleCountByBlockHash)] = &Endpoints::getUncleCountByBlockHash;
    m_handlers[methodString(EthMethod::eth_getUncleCountByBlockNumber)] = &Endpoints::getUncleCountByBlockNumber;
    m_handlers[methodString(EthMethod::eth_getCode)] = &Endpoints::getCode;
    m_handlers[methodString(EthMethod::eth_sign)] = &Endpoints::sign;
    m_handlers[methodString(EthMethod::eth_sendTransaction)] = &Endpoints::sendTransaction;
    m_handlers[methodString(EthMethod::eth_signTransaction)] = &Endpoints::signTransaction;
    m_handlers[methodString(EthMethod::eth_sendRawTransaction)] = &Endpoints::sendRawTransaction;
    m_handlers[methodString(EthMethod::eth_call)] = &Endpoints::call;
    m_handlers[methodString(EthMethod::eth_estimateGas)] = &Endpoints::estimateGas;
    m_handlers[methodString(EthMethod::eth_getBlockByHash)] = &Endpoints::getBlockByHash;
    m_handlers[methodString(EthMethod::eth_getBlockByNumber)] = &Endpoints::getBlockByNumber;
    m_handlers[methodString(EthMethod::eth_getTransactionByHash)] = &Endpoints::getTransactionByHash;
    m_handlers[methodString(EthMethod::eth_getTransactionByBlockHashAndIndex)] = &Endpoints::getTransactionByBlockHashAndIndex;
    m_handlers[methodString(EthMethod::eth_getTransactionByBlockNumberAndIndex)] = &Endpoints::getTransactionByBlockNumberAndIndex;
    m_handlers[methodString(EthMethod::eth_getTransactionReceipt)] = &Endpoints::getTransactionReceipt;
    m_handlers[methodString(EthMethod::eth_getUncleByBlockHashAndIndex)] = &Endpoints::getUncleByBlockHashAndIndex;
    m_handlers[methodString(EthMethod::eth_getUncleByBlockNumberAndIndex)] = &Endpoints::getUncleByBlockNumberAndIndex;
    m_handlers[methodString(EthMethod::eth_newFilter)] = &Endpoints::newFilter;
    m_handlers[methodString(EthMethod::eth_newBlockFilter)] = &Endpoints::newBlockFilter;
    m_handlers[methodString(EthMethod::eth_newPendingTransactionFilter)] = &Endpoints::newPendingTransactionFilter;
    m_handlers[methodString(EthMethod::eth_uninstallFilter)] = &Endpoints::uninstallFilter;
    m_handlers[methodString(EthMethod::eth_getFilterChanges)] = &Endpoints::getFilterChanges;
    m_handlers[methodString(EthMethod::eth_getFilterLogs)] = &Endpoints::getFilterLogs;
    m_handlers[methodString(EthMethod::eth_getLogs)] = &Endpoints::getLogs;
    // clang-format on
}

void EndpointsMapping::addNetHandlers()
{
    // clang-format off
    m_handlers[methodString(EthMethod::net_version)] = &Endpoints::verison;
    m_handlers[methodString(EthMethod::net_peerCount)] = &Endpoints::peerCount;
    m_handlers[methodString(EthMethod::net_listening)] = &Endpoints::listening;
    // clang-format on
}

void EndpointsMapping::addWeb3Handlers()
{
    // clang-format off
    m_handlers[methodString(EthMethod::web3_clientVersion)] = &Endpoints::clientVersion;
    m_handlers[methodString(EthMethod::web3_sha3)] = &Endpoints::sha3;
    // clang-format on
}
}  // namespace bcos::rpc