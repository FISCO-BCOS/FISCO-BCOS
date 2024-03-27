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
#include "EndpointInterface.h"
#include "bcos-rpc/groupmgr/GroupManager.h"
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
    void initMethod();
    MethodMap&& exportMethods() override { return std::move(m_methods); }
    void protocolVersion(RespFunc);
    void syning(RespFunc);
    void coinbase(RespFunc);
    void chainId(RespFunc);
    void mining(RespFunc);
    void hashrate(RespFunc);
    void gasPrice(RespFunc);
    void accounts(RespFunc);
    void blockNumber(RespFunc);
    void getBalance(std::string_view, std::string_view, RespFunc);
    void getStorageAt(std::string_view, std::string_view, std::string_view, RespFunc);
    void getTransactionCount(std::string_view, std::string_view, RespFunc);
    void getBlockTxCountByHash(std::string_view, RespFunc);
    void getBlockTxCountByNumber(std::string_view, RespFunc);
    void getUncleCountByBlockHash(std::string_view, RespFunc);
    void getUncleCountByBlockNumber(std::string_view, RespFunc);
    void getCode(std::string_view, std::string_view, RespFunc);
    void sign(std::string_view, std::string_view, RespFunc);
    void signTransaction(Json::Value const&, RespFunc);
    void sendTransaction(Json::Value const&, RespFunc);
    void sendRawTransaction(std::string_view, RespFunc);
    void call(Json::Value const&, std::string_view, RespFunc);
    void estimateGas(Json::Value const&, std::string_view, RespFunc);
    void getBlockByHash(std::string_view, bool, RespFunc);
    void getBlockByNumber(std::string_view, bool, RespFunc);
    void getTransactionByHash(std::string_view, RespFunc);
    void getTransactionByBlockHashAndIndex(std::string_view, std::string_view, RespFunc);
    void getTransactionByBlockNumberAndIndex(std::string_view, std::string_view, RespFunc);
    void getTransactionReceipt(std::string_view, RespFunc);
    void getUncleByBlockHashAndIndex(std::string_view, std::string_view, RespFunc);
    void getUncleByBlockNumberAndIndex(std::string_view, std::string_view, RespFunc);
    void newFilter(Json::Value const&, RespFunc);
    void newBlockFilter(RespFunc);
    void newPendingTransactionFilter(RespFunc);
    void uninstallFilter(std::string_view, RespFunc);
    void getFilterChanges(std::string_view, RespFunc);
    void getFilterLogs(std::string_view, RespFunc);
    void getLogs(Json::Value const&, RespFunc);

protected:
    std::string_view toView(const Json::Value& value)
    {
        const char* begin = nullptr;
        const char* end = nullptr;
        if (!value.getString(&begin, &end))
        {
            return {};
        }
        std::string_view view(begin, end - begin);
        return view;
    }
    void protocolVersionInterface(Json::Value const&, RespFunc func)
    {
        protocolVersion(std::move(func));
    }
    void syningInterface(Json::Value const&, RespFunc func) { syning(std::move(func)); }
    void coinbaseInterface(Json::Value const&, RespFunc func) { coinbase(std::move(func)); }
    void chainIdInterface(Json::Value const&, RespFunc func) { chainId(std::move(func)); }
    void miningInterface(Json::Value const&, RespFunc func) { mining(std::move(func)); }
    void hashrateInterface(Json::Value const&, RespFunc func) { hashrate(std::move(func)); }
    void gasPriceInterface(Json::Value const&, RespFunc func) { gasPrice(std::move(func)); }
    void accountsInterface(Json::Value const&, RespFunc func) { accounts(std::move(func)); }
    void blockNumberInterface(Json::Value const&, RespFunc func) { blockNumber(std::move(func)); }
    void getBalanceInterface(Json::Value const& req, RespFunc func)
    {
        getBalance(toView(req[0U]), toView(req[1U]), std::move(func));
    }
    void getStorageAtInterface(Json::Value const& req, RespFunc func)
    {
        getStorageAt(toView(req[0U]), toView(req[1U]), toView(req[2U]), std::move(func));
    }
    void getTransactionCountInterface(Json::Value const& req, RespFunc func)
    {
        getTransactionCount(toView(req[0U]), toView(req[1U]), std::move(func));
    }
    void getBlockTxCountByHashInterface(Json::Value const& req, RespFunc func)
    {
        getBlockTxCountByHash(toView(req[0U]), std::move(func));
    }
    void getBlockTxCountByNumberInterface(Json::Value const& req, RespFunc func)
    {
        getBlockTxCountByNumber(toView(req[0U]), std::move(func));
    }
    void getUncleCountByBlockHashInterface(Json::Value const& req, RespFunc func)
    {
        getUncleCountByBlockHash(toView(req[0U]), std::move(func));
    }
    void getUncleCountByBlockNumberInterface(Json::Value const& req, RespFunc func)
    {
        getUncleCountByBlockNumber(toView(req[0U]), std::move(func));
    }
    void getCodeInterface(Json::Value const& req, RespFunc func)
    {
        getCode(toView(req[0U]), toView(req[1U]), std::move(func));
    }
    void signInterface(Json::Value const& req, RespFunc func)
    {
        sign(toView(req[0U]), toView(req[1U]), std::move(func));
    }
    void signTransactionInterface(Json::Value const& req, RespFunc func)
    {
        signTransaction(req[0U], std::move(func));
    }
    void sendTransactionInterface(Json::Value const& req, RespFunc func)
    {
        sendTransaction(req[0U], std::move(func));
    }
    void sendRawTransactionInterface(Json::Value const& req, RespFunc func)
    {
        sendRawTransaction(toView(req[0U]), std::move(func));
    }
    void callInterface(Json::Value const& req, RespFunc func)
    {
        call(req[0U], toView(req[1U]), std::move(func));
    }
    void estimateGasInterface(Json::Value const& req, RespFunc func)
    {
        estimateGas(req[0U], toView(req[1U]), std::move(func));
    }
    void getBlockByHashInterface(Json::Value const& req, RespFunc func)
    {
        getBlockByHash(toView(req[0U]), req[1U].asBool(), std::move(func));
    }
    void getBlockByNumberInterface(Json::Value const& req, RespFunc func)
    {
        getBlockByNumber(toView(req[0U]), req[1U].asBool(), std::move(func));
    }
    void getTransactionByHashInterface(Json::Value const& req, RespFunc func)
    {
        getTransactionByHash(toView(req[0U]), std::move(func));
    }
    void getTransactionByBlockHashAndIndexInterface(Json::Value const& req, RespFunc func)
    {
        getTransactionByBlockHashAndIndex(toView(req[0U]), toView(req[1U]), std::move(func));
    }
    void getTransactionByBlockNumberAndIndexInterface(Json::Value const& req, RespFunc func)
    {
        getTransactionByBlockNumberAndIndex(toView(req[0U]), toView(req[1U]), std::move(func));
    }
    void getTransactionReceiptInterface(Json::Value const& req, RespFunc func)
    {
        getTransactionReceipt(toView(req[0U]), std::move(func));
    }
    void getUncleByBlockHashAndIndexInterface(Json::Value const& req, RespFunc func)
    {
        getUncleByBlockHashAndIndex(toView(req[0U]), toView(req[1U]), std::move(func));
    }
    void getUncleByBlockNumberAndIndexInterface(Json::Value const& req, RespFunc func)
    {
        getUncleByBlockNumberAndIndex(toView(req[0U]), toView(req[1U]), std::move(func));
    }
    void newFilterInterface(Json::Value const& req, RespFunc func)
    {
        newFilter(req[0], std::move(func));
    }
    void newBlockFilterInterface(Json::Value const&, RespFunc func)
    {
        newBlockFilter(std::move(func));
    }
    void newPendingTransactionFilterInterface(Json::Value const&, RespFunc func)
    {
        newPendingTransactionFilter(std::move(func));
    }
    void uninstallFilterInterface(Json::Value const& req, RespFunc func)
    {
        uninstallFilter(toView(req[0U]), std::move(func));
    }
    void getFilterChangesInterface(Json::Value const& req, RespFunc func)
    {
        getFilterChanges(toView(req[0U]), std::move(func));
    }
    void getFilterLogsInterface(Json::Value const& req, RespFunc func)
    {
        getFilterLogs(toView(req[0U]), std::move(func));
    }
    void getLogsInterface(Json::Value const& req, RespFunc func)
    {
        getLogs(req[0U], std::move(func));
    }

private:
    bcos::rpc::GroupManager::Ptr m_groupManager;
};

}  // namespace bcos::rpc
