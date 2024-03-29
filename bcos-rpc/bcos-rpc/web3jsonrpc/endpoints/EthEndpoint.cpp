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
 * @file EthEndpoint.cpp
 * @author: kyonGuo
 * @date 2024/3/21
 */

#include "EthEndpoint.h"

#include <bcos-rpc/Common.h>
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EthMethods.h>

using namespace bcos;
using namespace bcos::rpc;

task::Task<void> EthEndpoint::protocolVersion(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::syning(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::coinbase(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::chainId(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::mining(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::hashrate(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::gasPrice(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::accounts(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::blockNumber(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getBalance(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getStorageAt(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getTransactionCount(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getBlockTxCountByHash(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getBlockTxCountByNumber(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getUncleCountByBlockHash(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getUncleCountByBlockNumber(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getCode(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::sign(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::signTransaction(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::sendTransaction(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::sendRawTransaction(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::call(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::estimateGas(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getBlockByHash(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getBlockByNumber(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getTransactionByHash(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getTransactionByBlockHashAndIndex(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getTransactionByBlockNumberAndIndex(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getTransactionReceipt(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getUncleByBlockHashAndIndex(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getUncleByBlockNumberAndIndex(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::newFilter(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::newBlockFilter(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::newPendingTransactionFilter(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::uninstallFilter(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getFilterChanges(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getFilterLogs(const Json::Value&, Json::Value&)
{
    co_return;
}
task::Task<void> EthEndpoint::getLogs(const Json::Value&, Json::Value&)
{
    co_return;
}
