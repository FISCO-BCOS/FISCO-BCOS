/**
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
 * @brief: remote RPC via Boost Beast HTTP JSON-RPC client
 * @file: RemoteRpcConnection.h
 */

#pragma once

#include "../config/ConsoleConfig.h"
#include "RpcConnection.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <atomic>
#include <memory>
#include <string>

namespace bcos::console
{

// Remote RPC via HTTP JSON-RPC using Boost Beast.
// Connects to a FISCO BCOS node's RPC HTTP interface.
class RemoteRpcConnection : public RpcConnection,
                            public std::enable_shared_from_this<RemoteRpcConnection>
{
public:
    using Ptr = std::shared_ptr<RemoteRpcConnection>;

    explicit RemoteRpcConnection(ConsoleConfig const& config);
    ~RemoteRpcConnection() override;

    // ---- Transaction ----
    void call(std::string_view groupID, std::string_view nodeName, std::string_view to,
        std::string_view data, RpcRespFunc respFunc) override;
    void call(std::string_view groupID, std::string_view nodeName, std::string_view to,
        std::string_view data, std::string_view sign, RpcRespFunc respFunc) override;
    void sendTransaction(std::string_view groupID, std::string_view nodeName,
        std::string_view data, bool requireProof, RpcRespFunc respFunc) override;

    // ---- Transaction Query ----
    void getTransaction(std::string_view groupID, std::string_view nodeName,
        std::string_view txHash, bool requireProof, RpcRespFunc respFunc) override;
    void getTransactionReceipt(std::string_view groupID, std::string_view nodeName,
        std::string_view txHash, bool requireProof, RpcRespFunc respFunc) override;

    // ---- Block Query ----
    void getBlockByHash(std::string_view groupID, std::string_view nodeName,
        std::string_view blockHash, bool onlyHeader, bool onlyTxHash, RpcRespFunc respFunc) override;
    void getBlockByNumber(std::string_view groupID, std::string_view nodeName,
        int64_t blockNumber, bool onlyHeader, bool onlyTxHash, RpcRespFunc respFunc) override;
    void getBlockHashByNumber(std::string_view groupID, std::string_view nodeName,
        int64_t blockNumber, RpcRespFunc respFunc) override;
    void getBlockNumber(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) override;

    // ---- Contract ----
    void getCode(std::string_view groupID, std::string_view nodeName,
        std::string_view contractAddress, RpcRespFunc respFunc) override;
    void getABI(std::string_view groupID, std::string_view nodeName,
        std::string_view contractAddress, RpcRespFunc respFunc) override;

    // ---- Consensus ----
    void getSealerList(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) override;
    void getObserverList(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) override;
    void getNodeListByType(std::string_view groupID, std::string_view nodeName,
        std::string_view nodeType, RpcRespFunc respFunc) override;
    void getPbftView(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) override;
    void getConsensusStatus(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) override;
    void getSyncStatus(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) override;

    // ---- System ----
    void getSystemConfigByKey(std::string_view groupID, std::string_view nodeName,
        std::string_view key, RpcRespFunc respFunc) override;
    void getPendingTxSize(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) override;
    void getTotalTransactionCount(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) override;

    // ---- Group / Peers ----
    void getPeers(RpcRespFunc respFunc) override;
    void getGroupList(RpcRespFunc respFunc) override;
    void getGroupInfo(std::string_view groupID, RpcRespFunc respFunc) override;
    void getGroupInfoList(RpcRespFunc respFunc) override;
    void getGroupNodeInfo(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) override;
    void getGroupPeers(std::string_view groupID, RpcRespFunc respFunc) override;
    void getGroupBlockNumber(RpcRespFunc respFunc) override;

    // ---- Connection management ----
    bool isConnected() const override { return m_connected.load(); }
    void connect() override;
    void disconnect() override;

private:
    // Send a JSON-RPC request and parse the response.
    void sendJsonRpc(std::string method, Json::Value params, RpcRespFunc respFunc);

    // Template helper: serialize args into a Json params array and call sendJsonRpc.
    // Each arg is appended directly to the JSON array (string, int64_t, bool, etc.).
    template <typename... Args>
    void callRpc(std::string_view rpcName, RpcRespFunc respFunc, Args&&... args)
    {
        Json::Value p(Json::arrayValue);
        (p.append(std::forward<Args>(args)), ...);
        sendJsonRpc(std::string(rpcName), std::move(p), std::move(respFunc));
    }

    // Resolve groupID / nodeName from defaults if empty.
    std::string resolveGroup(std::string_view groupID) const
    {
        return groupID.empty() ? m_config.defaultGroup : std::string(groupID);
    }
    std::string resolveNode(std::string_view nodeName) const
    {
        return nodeName.empty() ? m_defaultNodeName : std::string(nodeName);
    }

    ConsoleConfig m_config;
    boost::asio::io_context m_ioc;
    std::atomic<bool> m_connected{false};
    std::string m_host;
    std::string m_port;
};

}  // namespace bcos::console
