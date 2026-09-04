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
 * @brief: abstract RPC connection for the console
 * @file: RpcConnection.h
 */

#pragma once

#include <bcos-utilities/Error.h>
#include <json/json.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace bcos::console
{

// Response callback for async RPC calls.
// error: nullptr on success; result: JSON result node.
using RpcRespFunc = std::function<void(bcos::Error::Ptr error, Json::Value& result)>;

// Abstract RPC connection used by all console commands.
// Implementations: LocalRpcConnection (in-process), SdkRpcConnection (WebSocket).
class RpcConnection
{
public:
    using Ptr = std::shared_ptr<RpcConnection>;
    virtual ~RpcConnection() = default;

    // ---- Transaction ----
    virtual void call(std::string_view groupID, std::string_view nodeName, std::string_view to,
        std::string_view data, RpcRespFunc respFunc) = 0;

    virtual void call(std::string_view groupID, std::string_view nodeName, std::string_view to,
        std::string_view data, std::string_view sign, RpcRespFunc respFunc) = 0;

    virtual void sendTransaction(std::string_view groupID, std::string_view nodeName,
        std::string_view data, bool requireProof, RpcRespFunc respFunc) = 0;

    // ---- Transaction Query ----
    virtual void getTransaction(std::string_view groupID, std::string_view nodeName,
        std::string_view txHash, bool requireProof, RpcRespFunc respFunc) = 0;

    virtual void getTransactionReceipt(std::string_view groupID, std::string_view nodeName,
        std::string_view txHash, bool requireProof, RpcRespFunc respFunc) = 0;

    // ---- Block Query ----
    virtual void getBlockByHash(std::string_view groupID, std::string_view nodeName,
        std::string_view blockHash, bool onlyHeader, bool onlyTxHash, RpcRespFunc respFunc) = 0;

    virtual void getBlockByNumber(std::string_view groupID, std::string_view nodeName,
        int64_t blockNumber, bool onlyHeader, bool onlyTxHash, RpcRespFunc respFunc) = 0;

    virtual void getBlockHashByNumber(std::string_view groupID, std::string_view nodeName,
        int64_t blockNumber, RpcRespFunc respFunc) = 0;

    virtual void getBlockNumber(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) = 0;

    // ---- Contract ----
    virtual void getCode(std::string_view groupID, std::string_view nodeName,
        std::string_view contractAddress, RpcRespFunc respFunc) = 0;

    virtual void getABI(std::string_view groupID, std::string_view nodeName,
        std::string_view contractAddress, RpcRespFunc respFunc) = 0;

    // ---- Consensus ----
    virtual void getSealerList(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) = 0;

    virtual void getObserverList(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) = 0;

    virtual void getNodeListByType(std::string_view groupID, std::string_view nodeName,
        std::string_view nodeType, RpcRespFunc respFunc) = 0;

    virtual void getPbftView(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) = 0;

    virtual void getConsensusStatus(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) = 0;

    virtual void getSyncStatus(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) = 0;

    // ---- System ----
    virtual void getSystemConfigByKey(std::string_view groupID, std::string_view nodeName,
        std::string_view key, RpcRespFunc respFunc) = 0;

    virtual void getPendingTxSize(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) = 0;

    virtual void getTotalTransactionCount(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) = 0;

    // ---- Group / Peers ----
    virtual void getPeers(RpcRespFunc respFunc) = 0;

    virtual void getGroupList(RpcRespFunc respFunc) = 0;

    virtual void getGroupInfo(std::string_view groupID, RpcRespFunc respFunc) = 0;

    virtual void getGroupInfoList(RpcRespFunc respFunc) = 0;

    virtual void getGroupNodeInfo(
        std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc) = 0;

    virtual void getGroupPeers(std::string_view groupID, RpcRespFunc respFunc) = 0;

    virtual void getGroupBlockNumber(RpcRespFunc respFunc) = 0;

    // ---- Connection management ----
    virtual bool isConnected() const = 0;

    virtual void connect() = 0;

    virtual void disconnect() = 0;

    // ---- Default node name (for routing) ----
    virtual std::string defaultNodeName() const { return m_defaultNodeName; }

    virtual void setDefaultNodeName(std::string name) { m_defaultNodeName = std::move(name); }

    virtual void clearDefaultNodeName() { m_defaultNodeName.clear(); }

protected:
    std::string m_defaultNodeName;
};

}  // namespace bcos::console
