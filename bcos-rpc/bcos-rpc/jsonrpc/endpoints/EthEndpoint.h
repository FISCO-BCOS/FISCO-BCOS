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
 * @file EthEndpoint.h
 * @author: kyonGuo
 * @date 2024/3/21
 */

#pragma once
#include "bcos-rpc/groupmgr/GroupManager.h"
#include "bcos-rpc/jsonrpc/endpoints/EndpointInterface.h"
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <json/json.h>
#include <tbb/concurrent_hash_map.h>
#include <boost/core/ignore_unused.hpp>
#include <unordered_map>

namespace bcos::rpc
{

/**
 * eth entry point to match 'eth_' methods
 */
class EthEndpoint : public EndpointInterface
{
public:
    explicit EthEndpoint(bcos::rpc::GroupManager::Ptr group_manager);
    std::string getEntryName() const override { return "eth"; }
    MethodMap const& exportMethods() override { return m_methods; }
    void protoclVersion(RespFunc) {}
    void syning(RespFunc) {}
    void coinbase(RespFunc) {}
    void chainId(RespFunc) {}
    void mining(RespFunc) {}
    void hashrate(RespFunc) {}
    void gasPrice(RespFunc) {}
    void accounts(RespFunc) {}
    void blockNumber(RespFunc) {}
    void getBalance(std::string_view, std::string_view, RespFunc) {}
    void getStorageAt(std::string_view, std::string_view, std::string_view, RespFunc) {}
    void getTransactionCount(std::string_view, std::string_view, RespFunc) {}
    void getBlockTxCountByHash(std::string_view, RespFunc) {}
    void getBlockTxCountByNumber(std::string_view, RespFunc) {}
    void getUncleCountByBlockHash(std::string_view, RespFunc) {}
    void getUncleCountByBlockNumber(std::string_view, RespFunc) {}
    void getCode(std::string_view, std::string_view, RespFunc) {}
    void sign(std::string_view, std::string_view, RespFunc) {}
    void signTransaction(Json::Value const&, RespFunc) {}
    void sendTransaction(Json::Value const&, RespFunc) {}
    void sendRawTransaction(std::string_view, RespFunc) {}
    void call(Json::Value const&, std::string_view, RespFunc) {}
    void estimateGas(Json::Value const&, std::string_view, RespFunc) {}
    void getBlockByHash(std::string_view, bool, RespFunc) {}
    void getBlockByNumber(std::string_view, bool, RespFunc) {}
    void getTransactionByHash(std::string_view, RespFunc) {}
    void getTransactionByBlockHashAndIndex(std::string_view, std::string_view, RespFunc) {}
    void getTransactionByBlockNumberAndIndex(std::string_view, std::string_view, RespFunc) {}
    void getTransactionReceipt(std::string_view, RespFunc) {}
    void getUncleByBlockHashAndIndex(std::string_view, std::string_view, RespFunc) {}
    void getUncleByBlockNumberAndIndex(std::string_view, std::string_view, RespFunc) {}
    void newFilter(Json::Value const&, RespFunc) {}
    void newBlockFilter(RespFunc) {}
    void newPendingTransactionFilter(RespFunc) {}
    void uninstallFilter(std::string_view, RespFunc) {}
    void getFilterChanges(std::string_view, RespFunc) {}
    void getFilterLogs(std::string_view, RespFunc) {}
    void getLogs(Json::Value const&, RespFunc) {}

private:
    bcos::rpc::GroupManager::Ptr m_groupManager;
};

}  // namespace bcos::rpc
