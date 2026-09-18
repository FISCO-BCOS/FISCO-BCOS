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
 * @brief: SdkRpcConnection — thin wrapper over bcos-cpp-sdk JsonRpcImpl+Service.
 *         Replaces the hand-written HTTP client with SDK's WebSocket-based
 *         RPC layer, gaining connection pooling, auto-reconnect, block-number
 *         tracking, and multi-node load balancing for free.
 * @file: SdkRpcConnection.h
 */

#pragma once

#include "RpcConnection.h"

#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-cpp-sdk/rpc/JsonRpcImpl.h>
#include <bcos-cpp-sdk/ws/Service.h>
#include <memory>
#include <string>

namespace bcos::console
{

class ConsoleConfig;

/**
 * @brief RPC connection backed by bcos-cpp-sdk's WebSocket service.
 *
 * Provides persistent WebSocket connections with auto-reconnect,
 * multi-node connection pooling, automatic block-number tracking,
 * and delegates all 25 RPC methods to SDK (zero hand-written boilerplate).
 */
class SdkRpcConnection : public RpcConnection
{
public:
    SdkRpcConnection() = default;
    ~SdkRpcConnection() override;

    // ---- Connection management ----
    bool isConnected() const override;
    void connect() override;
    void disconnect() override;

    /**
     * @brief Configure the connection from ConsoleConfig before connect().
     *
     * Must be called before connect().
     */
    bool configure(const ConsoleConfig& config);

    // ---- Transaction ----
    void call(std::string_view groupID, std::string_view nodeName, std::string_view to,
        std::string_view data, RpcRespFunc respFunc) override;

    void call(std::string_view groupID, std::string_view nodeName, std::string_view to,
        std::string_view data, std::string_view sign, RpcRespFunc respFunc) override;

    void sendTransaction(std::string_view groupID, std::string_view nodeName, std::string_view data,
        bool requireProof, RpcRespFunc respFunc) override;

    // ---- Transaction Query ----
    void getTransaction(std::string_view groupID, std::string_view nodeName,
        std::string_view txHash, bool requireProof, RpcRespFunc respFunc) override;

    void getTransactionReceipt(std::string_view groupID, std::string_view nodeName,
        std::string_view txHash, bool requireProof, RpcRespFunc respFunc) override;

    // ---- Block Query ----
    void getBlockByHash(std::string_view groupID, std::string_view nodeName,
        std::string_view blockHash, bool onlyHeader, bool onlyTxHash,
        RpcRespFunc respFunc) override;

    void getBlockByNumber(std::string_view groupID, std::string_view nodeName, int64_t blockNumber,
        bool onlyHeader, bool onlyTxHash, RpcRespFunc respFunc) override;

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

    /// Direct access to underlying SDK components (for TransactionPipeline etc.)
    bcos::cppsdk::jsonrpc::JsonRpcImpl::Ptr sdkJsonRpc() const { return m_jsonRpc; }
    bcos::cppsdk::service::Service::Ptr sdkService() const { return m_service; }

private:
    /// Build WsConfig from ConsoleConfig
    static std::shared_ptr<bcos::boostssl::ws::WsConfig> buildWsConfig(const ConsoleConfig& config);

    bcos::cppsdk::service::Service::Ptr m_service;
    bcos::cppsdk::jsonrpc::JsonRpcImpl::Ptr m_jsonRpc;
    bool m_configured = false;
};

/// Adapt SDK's bytes-based callback to our Json::Value-based callback.
/// Defined in SdkRpcConnection.cpp; used by the delegate template.
bcos::cppsdk::jsonrpc::RespFunc adaptCallback(RpcRespFunc consoleCallback);

}  // namespace bcos::console
