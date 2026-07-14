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
 * @brief: SdkRpcConnection implementation — thin delegation to bcos-cpp-sdk
 * @file: SdkRpcConnection.cpp
 */

#include "SdkRpcConnection.h"
#include "../config/ConsoleConfig.h"

#include <bcos-cpp-sdk/SdkFactory.h>
#include <bcos-utilities/Common.h>
#include <iostream>

namespace bcos::console
{

// ============================================================================
// Callback adapter: SDK (Error::Ptr, bytes) → Console (Error::Ptr, Json::Value&)
// ============================================================================

bcos::cppsdk::jsonrpc::RespFunc adaptCallback(RpcRespFunc consoleCallback)
{
    return [cb = std::move(consoleCallback)](bcos::Error::Ptr error, bcos::bytes resp) mutable {
        if (error)
        {
            Json::Value nullResult;
            cb(std::move(error), nullResult);
            return;
        }
        // Parse bytes as JSON
        std::string jsonStr(resp.begin(), resp.end());
        Json::Value result;
        Json::Reader reader;
        if (!reader.parse(jsonStr, result))
        {
            // If not valid JSON, return the raw string in a Value
            result = jsonStr;
        }
        cb(nullptr, result);
    };
}

// ============================================================================
// WsConfig construction from ConsoleConfig (TOML → SDK config)
// ============================================================================

std::shared_ptr<bcos::boostssl::ws::WsConfig> SdkRpcConnection::buildWsConfig(
    const ConsoleConfig& config)
{
    auto wsConfig = std::make_shared<bcos::boostssl::ws::WsConfig>();
    wsConfig->setModel(bcos::boostssl::ws::WsModel::Client);

    // Peers
    auto endpoints = std::make_shared<std::set<bcos::boostssl::NodeIPEndpoint>>();
    for (auto const& peer : config.peers)
    {
        auto colonPos = peer.find(':');
        if (colonPos != std::string::npos)
        {
            endpoints->emplace(peer.substr(0, colonPos),
                static_cast<uint16_t>(std::stoi(peer.substr(colonPos + 1))));
        }
    }
    wsConfig->setConnectPeers(endpoints);

    // SSL
    wsConfig->setDisableSsl(config.disableSsl);
    if (!config.disableSsl)
    {
        auto ctxConfig = std::make_shared<bcos::boostssl::context::ContextConfig>();
        ctxConfig->setSslType(config.useSMCrypto ? "sm_ssl" : "ssl");
        if (!config.useSMCrypto)
        {
            bcos::boostssl::context::ContextConfig::CertConfig cert;
            cert.caCert = config.caCert;
            cert.nodeCert = config.sslCert;
            cert.nodeKey = config.sslKey;
            ctxConfig->setCertConfig(cert);
        }
        else
        {
            bcos::boostssl::context::ContextConfig::SMCertConfig smCert;
            smCert.caCert = config.smCaCert;
            smCert.nodeCert = config.smSslCert;
            smCert.nodeKey = config.smSslKey;
            smCert.enNodeCert = config.smEnSslCert;
            smCert.enNodeKey = config.smEnSslKey;
            ctxConfig->setSmCertConfig(smCert);
        }
        wsConfig->setContextConfig(ctxConfig);
    }

    // Thread pool
    wsConfig->setThreadPoolSize(
        config.threadPoolSize > 0 ? static_cast<uint32_t>(config.threadPoolSize) : 4);

    // Timeouts
    wsConfig->setSendMsgTimeout(static_cast<int32_t>(config.messageTimeout));
    wsConfig->setReconnectPeriod(10000);  // 10s reconnect
    wsConfig->setHeartbeatPeriod(10000);  // 10s heartbeat

    return wsConfig;
}

// ============================================================================
// Connection lifecycle
// ============================================================================

SdkRpcConnection::~SdkRpcConnection()
{
    disconnect();
}

bool SdkRpcConnection::configure(const ConsoleConfig& config)
{
    try
    {
        auto wsConfig = buildWsConfig(config);
        auto factory = std::make_shared<bcos::cppsdk::SdkFactory>();
        m_service = factory->buildService(wsConfig);
        m_jsonRpc = factory->buildJsonRpc(m_service, true);
        m_configured = true;
        return true;
    }
    catch (std::exception const& e)
    {
        std::cerr << "SdkRpcConnection config error: " << e.what() << '\n';
        return false;
    }
}

void SdkRpcConnection::connect()
{
    if (!m_configured)
    {
        std::cerr << "SdkRpcConnection: not configured, call configure() first\n";
        return;
    }
    try
    {
        m_service->start();
        // Wait for at least one connection to establish
        m_service->waitForConnectionEstablish();
        std::cout << "[SDK] Connected via WebSocket\n";
    }
    catch (std::exception const& e)
    {
        std::cerr << "SdkRpcConnection connect error: " << e.what() << '\n';
    }
}

void SdkRpcConnection::disconnect()
{
    if (m_service)
    {
        m_service->stop();
    }
}

bool SdkRpcConnection::isConnected() const
{
    // Check if we have any active endpoint for any group
    if (!m_service)
        return false;
    // The Service tracks connections internally; we approximate by checking
    // if the service has been started
    return true;  // Service manages connection state internally
}

// ============================================================================
// RPC method delegation template — replaces the old SDK_DELEGATE macro
// ============================================================================

// Delegate any SDK RPC method, auto-appending adaptCallback(respFunc).
template <typename MemFn, typename... CallArgs>
static void delegate(RpcRespFunc respFunc, bcos::cppsdk::jsonrpc::JsonRpcImpl::Ptr const& rpc,
    MemFn method, CallArgs&&... args)
{
    if (!rpc)
    {
        Json::Value e;
        respFunc(BCOS_ERROR_PTR(-1, "SdkRpcConnection not ready"), e);
        return;
    }
    ((*rpc).*method)(std::forward<CallArgs>(args)..., adaptCallback(std::move(respFunc)));
}


// ---- Transaction ----

void SdkRpcConnection::call(std::string_view groupID, std::string_view nodeName,
    std::string_view to, std::string_view data, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc, &bcos::cppsdk::jsonrpc::JsonRpcInterface::call,
        std::string(groupID), std::string(nodeName), std::string(to), std::string(data));
}

void SdkRpcConnection::call(std::string_view groupID, std::string_view nodeName,
    std::string_view to, std::string_view data, std::string_view sign, RpcRespFunc respFunc)
{
    // SDK's call doesn't have a signature parameter; use genericMethod
    if (!m_jsonRpc)
    {
        Json::Value e;
        respFunc(BCOS_ERROR_PTR(-1, "SdkRpcConnection not ready"), e);
        return;
    }
    // Build params manually for call with signature
    Json::Value params(Json::arrayValue);
    params.append(std::string(groupID));
    params.append(std::string(nodeName));
    params.append(std::string(to));
    params.append(std::string(data));
    params.append(std::string(sign));
    // Use genericMethod for extended call
    m_jsonRpc->genericMethod(std::string(groupID), std::string(nodeName), params.toStyledString(),
        adaptCallback(std::move(respFunc)));
}

void SdkRpcConnection::sendTransaction(std::string_view groupID, std::string_view nodeName,
    std::string_view data, bool requireProof, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::sendTransaction, std::string(groupID),
        std::string(nodeName), std::string(data), requireProof);
}

// ---- Transaction Query ----

void SdkRpcConnection::getTransaction(std::string_view groupID, std::string_view nodeName,
    std::string_view txHash, bool requireProof, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getTransaction, std::string(groupID),
        std::string(nodeName), std::string(txHash), requireProof);
}

void SdkRpcConnection::getTransactionReceipt(std::string_view groupID, std::string_view nodeName,
    std::string_view txHash, bool requireProof, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getTransactionReceipt, std::string(groupID),
        std::string(nodeName), std::string(txHash), requireProof);
}

// ---- Block Query ----

void SdkRpcConnection::getBlockByHash(std::string_view groupID, std::string_view nodeName,
    std::string_view blockHash, bool onlyHeader, bool onlyTxHash, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getBlockByHash, std::string(groupID),
        std::string(nodeName), std::string(blockHash), onlyHeader, onlyTxHash);
}

void SdkRpcConnection::getBlockByNumber(std::string_view groupID, std::string_view nodeName,
    int64_t blockNumber, bool onlyHeader, bool onlyTxHash, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getBlockByNumber, std::string(groupID),
        std::string(nodeName), blockNumber, onlyHeader, onlyTxHash);
}

void SdkRpcConnection::getBlockHashByNumber(
    std::string_view groupID, std::string_view nodeName, int64_t blockNumber, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getBlockHashByNumber, std::string(groupID),
        std::string(nodeName), blockNumber);
}

void SdkRpcConnection::getBlockNumber(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getBlockNumber, std::string(groupID),
        std::string(nodeName));
}

// ---- Contract ----

void SdkRpcConnection::getCode(std::string_view groupID, std::string_view nodeName,
    std::string_view contractAddress, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc, &bcos::cppsdk::jsonrpc::JsonRpcInterface::getCode,
        std::string(groupID), std::string(nodeName), std::string(contractAddress));
}

void SdkRpcConnection::getABI(std::string_view groupID, std::string_view nodeName,
    std::string_view contractAddress, RpcRespFunc respFunc)
{
    if (!m_jsonRpc)
    {
        Json::Value e;
        respFunc(BCOS_ERROR_PTR(-1, "SdkRpcConnection not ready"), e);
        return;
    }
    // getABI is not in SDK's JsonRpcInterface; build request manually
    Json::Value params(Json::arrayValue);
    params.append(std::string(groupID));
    params.append(std::string(contractAddress));
    Json::Value req;
    req["jsonrpc"] = "2.0";
    req["method"] = "getABI";
    req["params"] = params;
    req["id"] = 1;
    m_jsonRpc->genericMethod(std::string(groupID), std::string(nodeName),
        Json::FastWriter().write(req), adaptCallback(std::move(respFunc)));
}

// ---- Consensus ----

void SdkRpcConnection::getSealerList(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getSealerList, std::string(groupID),
        std::string(nodeName));
}

void SdkRpcConnection::getObserverList(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getObserverList, std::string(groupID),
        std::string(nodeName));
}

void SdkRpcConnection::getNodeListByType(std::string_view groupID, std::string_view nodeName,
    std::string_view nodeType, RpcRespFunc respFunc)
{
    if (!m_jsonRpc)
    {
        Json::Value e;
        respFunc(BCOS_ERROR_PTR(-1, "SdkRpcConnection not ready"), e);
        return;
    }
    Json::Value params(Json::arrayValue);
    params.append(std::string(groupID));
    params.append(std::string(nodeType));
    Json::Value req;
    req["jsonrpc"] = "2.0";
    req["method"] = "getNodeListByType";
    req["params"] = params;
    req["id"] = 1;
    m_jsonRpc->genericMethod(std::string(groupID), std::string(nodeName),
        Json::FastWriter().write(req), adaptCallback(std::move(respFunc)));
}

void SdkRpcConnection::getPbftView(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc, &bcos::cppsdk::jsonrpc::JsonRpcInterface::getPbftView,
        std::string(groupID), std::string(nodeName));
}

void SdkRpcConnection::getConsensusStatus(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getConsensusStatus, std::string(groupID),
        std::string(nodeName));
}

void SdkRpcConnection::getSyncStatus(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getSyncStatus, std::string(groupID),
        std::string(nodeName));
}

// ---- System ----

void SdkRpcConnection::getSystemConfigByKey(
    std::string_view groupID, std::string_view nodeName, std::string_view key, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getSystemConfigByKey, std::string(groupID),
        std::string(nodeName), std::string(key));
}

void SdkRpcConnection::getPendingTxSize(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getPendingTxSize, std::string(groupID),
        std::string(nodeName));
}

void SdkRpcConnection::getTotalTransactionCount(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getTotalTransactionCount, std::string(groupID),
        std::string(nodeName));
}

// ---- Group / Peers (no group param) ----

void SdkRpcConnection::getPeers(RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc, &bcos::cppsdk::jsonrpc::JsonRpcInterface::getPeers);
}

void SdkRpcConnection::getGroupList(RpcRespFunc respFunc)
{
    delegate(
        std::move(respFunc), m_jsonRpc, &bcos::cppsdk::jsonrpc::JsonRpcInterface::getGroupList);
}

void SdkRpcConnection::getGroupInfoList(RpcRespFunc respFunc)
{
    delegate(
        std::move(respFunc), m_jsonRpc, &bcos::cppsdk::jsonrpc::JsonRpcInterface::getGroupInfoList);
}

void SdkRpcConnection::getGroupBlockNumber(RpcRespFunc respFunc)
{
    if (!m_jsonRpc)
    {
        Json::Value e;
        respFunc(BCOS_ERROR_PTR(-1, "SdkRpcConnection not ready"), e);
        return;
    }
    Json::Value req;
    req["jsonrpc"] = "2.0";
    req["method"] = "getGroupBlockNumber";
    req["params"] = Json::Value(Json::arrayValue);
    req["id"] = 1;
    m_jsonRpc->genericMethod(Json::FastWriter().write(req), adaptCallback(std::move(respFunc)));
}

// ---- Group / Peers (group param only) ----

void SdkRpcConnection::getGroupPeers(std::string_view groupID, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getGroupPeers, std::string(groupID));
}

void SdkRpcConnection::getGroupInfo(std::string_view groupID, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc, &bcos::cppsdk::jsonrpc::JsonRpcInterface::getGroupInfo,
        std::string(groupID));
}

void SdkRpcConnection::getGroupNodeInfo(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    delegate(std::move(respFunc), m_jsonRpc,
        &bcos::cppsdk::jsonrpc::JsonRpcInterface::getGroupNodeInfo, std::string(groupID),
        std::string(nodeName));
}


}  // namespace bcos::console
