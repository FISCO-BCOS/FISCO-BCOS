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
 * @brief: in-process RPC bridge — directly calls JsonRpcInterface
 * @file: LocalRpcConnection.h
 */

#pragma once

#include "RpcConnection.h"
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <memory>
#include <mutex>

namespace bcos::console
{

// In-process connection that delegates directly to JsonRpcInterface.
// This avoids network overhead when the console is attached to a local node.
class LocalRpcConnection : public RpcConnection
{
public:
    using Ptr = std::shared_ptr<LocalRpcConnection>;

    explicit LocalRpcConnection(bcos::rpc::JsonRpcInterface::Ptr jsonRpc,
        std::string defaultGroup, std::string defaultNodeName = {});

    ~LocalRpcConnection() override = default;

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
    bool isConnected() const override { return m_jsonRpc != nullptr; }

    void connect() override {}

    void disconnect() override {}

    bcos::rpc::JsonRpcInterface::Ptr jsonRpc() const { return m_jsonRpc; }

    std::string defaultGroup() const { return m_defaultGroup; }

private:
    bcos::rpc::JsonRpcInterface::Ptr m_jsonRpc;
    std::string m_defaultGroup;
    mutable std::mutex m_mutex;
};

}  // namespace bcos::console
