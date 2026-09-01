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
 * @brief RpcFactory
 * @file RpcFactory.h
 * @author: octopus
 * @date 2021-07-15
 */

#include "bcos-rpc/amop/AirAMOPClient.h"
#include <bcos-boostssl/websocket/WsInitializer.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-framework/protocol/AMOPRequest.h>
#include <bcos-framework/security/KeyEncryptInterface.h>
#include <bcos-rpc/RpcFactory.h>
#include <bcos-rpc/event/EventSubMatcher.h>
#include <bcos-rpc/groupmgr/TarsGroupManager.h>
#include <bcos-rpc/jsonrpc/JsonRpcFilterSystem.h>
#include <bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h>
#include <bcos-rpc/jwtAuth/JwtConfig.h>
#include <bcos-rpc/jwtAuth/JwtVerifier.h>
#include <bcos-rpc/web3jsonrpc/Web3FilterSystem.h>
#include <bcos-tars-protocol/protocol/GroupInfoCodecImpl.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/FileUtility.h>
#include <bcos-utilities/NewTimer.h>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <bcos-utilities/BoostLog.h>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::gateway;
using namespace bcos::group;
using namespace bcos::boostssl::ws;
using namespace bcos::protocol;
using namespace bcos::security;

RpcFactory::RpcFactory(std::string _chainID, GatewayInterface::Ptr _gatewayInterface,
    KeyFactory::Ptr _keyFactory, bcos::security::KeyEncryptInterface::Ptr _dataEncrypt)
  : m_chainID(std::move(_chainID)),
    m_gateway(std::move(_gatewayInterface)),
    m_keyFactory(std::move(_keyFactory)),
    m_dataEncrypt(std::move(_dataEncrypt))
{}

std::shared_ptr<bcos::boostssl::ws::WsConfig> RpcFactory::initConfig(
    const bcos::tool::NodeConfig::Ptr& _nodeConfig)
{
    auto wsConfig = std::make_shared<boostssl::ws::WsConfig>();
    wsConfig->setModel(bcos::boostssl::ws::WsModel::Server);

    wsConfig->setListenIP(_nodeConfig->rpcListenIP());
    wsConfig->setListenPort(_nodeConfig->rpcListenPort());
    wsConfig->setDisableSsl(_nodeConfig->rpcDisableSsl());
    if (_nodeConfig->rpcDisableSsl())
    {
        RPC_LOG(INFO) << LOG_BADGE("initConfig") << LOG_DESC("rpc work in disable ssl model")
                      << LOG_KV("listenIP", wsConfig->listenIP())
                      << LOG_KV("listenPort", wsConfig->listenPort())
                      << LOG_KV("ioThreadCount", _nodeConfig->ioThreadCount())
                      << LOG_KV("asServer", wsConfig->asServer());
        return wsConfig;
    }

    auto contextConfig = std::make_shared<boostssl::context::ContextConfig>();
    if (!_nodeConfig->rpcSmSsl())
    {  //  ssl
        boostssl::context::ContextConfig::CertConfig certConfig;

        // caCert
        if (!_nodeConfig->caCert().empty())
        {
            try
            {
                bytes caCertContent = readContents(boost::filesystem::path(_nodeConfig->caCert()));
                if (!caCertContent.empty())
                {
                    certConfig.caCert.resize(caCertContent.size());
                    memcpy(certConfig.caCert.data(), caCertContent.data(), caCertContent.size());
                }
            }
            catch (std::exception& e)
            {
                BCOS_LOG(ERROR) << LOG_BADGE("RpcFactory") << LOG_DESC("open caCert failed")
                                << LOG_KV("file", _nodeConfig->caCert());
                BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                          "RpcFactory::initConfig: unable read content of key:" +
                                          _nodeConfig->caCert()));
            }
        }

        // nodeCert
        if (!_nodeConfig->nodeCert().empty())
        {
            try
            {
                bytes nodeCertContent =
                    readContents(boost::filesystem::path(_nodeConfig->nodeCert()));
                if (!nodeCertContent.empty())
                {
                    certConfig.nodeCert.resize(nodeCertContent.size());
                    memcpy(
                        certConfig.nodeCert.data(), nodeCertContent.data(), nodeCertContent.size());
                }
            }
            catch (std::exception& e)
            {
                BCOS_LOG(ERROR) << LOG_BADGE("RpcFactory") << LOG_DESC("open nodeCert failed")
                                << LOG_KV("file", _nodeConfig->nodeCert());
                BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                          "RpcFactory::initConfig: unable read content of key:" +
                                          _nodeConfig->nodeCert()));
            }
        }

        // nodeKey
        if (!_nodeConfig->nodeKey().empty())
        {
            bytes nodeKeyContent;
            std::shared_ptr<bytes> decrypted;
            try
            {
                if (nullptr == m_dataEncrypt) [[likely]]  // storage_security.enable = false
                {
                    nodeKeyContent = readContents(boost::filesystem::path(_nodeConfig->nodeKey()));
                }
                else
                {
                    decrypted = m_dataEncrypt->decryptFile(_nodeConfig->nodeKey());
                    if (decrypted)
                    {
                        nodeKeyContent = std::move(*decrypted);
                    }
                }
            }
            catch (std::exception& e)
            {
                BCOS_LOG(ERROR) << LOG_BADGE("RpcFactory") << LOG_DESC("open nodeKey failed")
                                << LOG_KV("file", _nodeConfig->nodeKey());
                BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                          "RpcFactory::initConfig: unable read content of key:" +
                                          _nodeConfig->nodeKey()));
            }
            // Reject OUTSIDE the try: thrown inside, these InvalidParameter
            // diagnostics would be caught above and replaced with the generic
            // "unable read content" message, hiding the distinction between a
            // missing file and a KMS/decrypt that returned no content.
            if (m_dataEncrypt && !decrypted)
            {
                BOOST_THROW_EXCEPTION(
                    InvalidParameter() << errinfo_comment(
                        "RpcFactory::initConfig: decryptFile returned no content for key:" +
                        _nodeConfig->nodeKey()));
            }
            if (nodeKeyContent.empty())
            {
                BOOST_THROW_EXCEPTION(
                    InvalidParameter()
                    << errinfo_comment("RpcFactory::initConfig: unable read content of key:" +
                                       _nodeConfig->nodeKey()));
            }
            certConfig.nodeKey.resize(nodeKeyContent.size());
            memcpy(certConfig.nodeKey.data(), nodeKeyContent.data(), nodeKeyContent.size());
        }

        contextConfig->setIsCertPath(false);

        contextConfig->setCertConfig(certConfig);
        contextConfig->setSslType("ssl");

        RPC_LOG(INFO) << LOG_DESC("rpc work in ssl model")
                      << LOG_KV("listenIP", wsConfig->listenIP())
                      << LOG_KV("listenPort", wsConfig->listenPort())
                      << LOG_KV("ioThreadCount", _nodeConfig->ioThreadCount())
                      << LOG_KV("asServer", wsConfig->asServer())
                      << LOG_KV("caCert", _nodeConfig->caCert())
                      << LOG_KV("nodeCert", _nodeConfig->nodeCert())
                      << LOG_KV("nodeKey", _nodeConfig->nodeKey());
    }
    else
    {  // sm ssl
        boostssl::context::ContextConfig::SMCertConfig certConfig;

        // caCert
        if (!_nodeConfig->smCaCert().empty())
        {
            try
            {
                bytes smCaCertContent =
                    readContents(boost::filesystem::path(_nodeConfig->smCaCert()));
                if (!smCaCertContent.empty())
                {
                    certConfig.caCert.resize(smCaCertContent.size());
                    memcpy(
                        certConfig.caCert.data(), smCaCertContent.data(), smCaCertContent.size());
                }
            }
            catch (std::exception& e)
            {
                BCOS_LOG(ERROR) << LOG_BADGE("RpcFactory") << LOG_DESC("open smCaCert failed")
                                << LOG_KV("file", _nodeConfig->caCert());
                BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                          "RpcFactory::initConfig: unable read content of key:" +
                                          _nodeConfig->caCert()));
            }
        }

        // nodeCert
        if (!_nodeConfig->smNodeCert().empty())
        {
            try
            {
                bytes smNodeCertContent =
                    readContents(boost::filesystem::path(_nodeConfig->smNodeCert()));
                if (!smNodeCertContent.empty())
                {
                    certConfig.nodeCert.resize(smNodeCertContent.size());
                    memcpy(certConfig.nodeCert.data(), smNodeCertContent.data(),
                        smNodeCertContent.size());
                }
            }
            catch (std::exception& e)
            {
                BCOS_LOG(ERROR) << LOG_BADGE("RpcFactory") << LOG_DESC("open smNodeCert failed")
                                << LOG_KV("file", _nodeConfig->nodeCert());
                BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                          "RpcFactory::initConfig: unable read content of key:" +
                                          _nodeConfig->nodeCert()));
            }
        }

        // nodeKey
        if (!_nodeConfig->smNodeKey().empty())
        {
            bytes smNodeKeyContent;
            std::shared_ptr<bytes> decrypted;
            try
            {
                if (nullptr == m_dataEncrypt)  // storage_security.enable = false
                {
                    smNodeKeyContent =
                        readContents(boost::filesystem::path(_nodeConfig->smNodeKey()));
                }
                else
                {
                    decrypted = m_dataEncrypt->decryptFile(_nodeConfig->smNodeKey());
                    if (decrypted)
                    {
                        smNodeKeyContent = std::move(*decrypted);
                    }
                }
            }
            catch (std::exception& e)
            {
                BCOS_LOG(ERROR) << LOG_BADGE("RpcFactory") << LOG_DESC("open smNodeKey failed")
                                << LOG_KV("file", _nodeConfig->nodeKey());
                BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                          "RpcFactory::initConfig: unable read content of key:" +
                                          _nodeConfig->nodeKey()));
            }
            // Reject OUTSIDE the try: see the nodeKey block above.
            if (m_dataEncrypt && !decrypted)
            {
                BOOST_THROW_EXCEPTION(
                    InvalidParameter() << errinfo_comment(
                        "RpcFactory::initConfig: decryptFile returned no content for key:" +
                        _nodeConfig->smNodeKey()));
            }
            if (smNodeKeyContent.empty())
            {
                BOOST_THROW_EXCEPTION(
                    InvalidParameter()
                    << errinfo_comment("RpcFactory::initConfig: unable read content of key:" +
                                       _nodeConfig->smNodeKey()));
            }
            certConfig.nodeKey.resize(smNodeKeyContent.size());
            memcpy(certConfig.nodeKey.data(), smNodeKeyContent.data(), smNodeKeyContent.size());
        }

        // enNodeCert
        if (!_nodeConfig->enSmNodeCert().empty())
        {
            try
            {
                bytes enSmNodeCertContent =
                    readContents(boost::filesystem::path(_nodeConfig->enSmNodeCert()));
                if (!enSmNodeCertContent.empty())
                {
                    certConfig.enNodeCert.resize(enSmNodeCertContent.size());
                    memcpy(certConfig.enNodeCert.data(), enSmNodeCertContent.data(),
                        enSmNodeCertContent.size());
                }
            }
            catch (std::exception& e)
            {
                BCOS_LOG(ERROR) << LOG_BADGE("RpcFactory") << LOG_DESC("open enSmNodeCert failed")
                                << LOG_KV("file", _nodeConfig->nodeCert());
                BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                          "RpcFactory::initConfig: unable read content of key:" +
                                          _nodeConfig->nodeCert()));
            }
        }

        // enNodeKey
        if (!_nodeConfig->enSmNodeKey().empty())
        {
            bytes enSmNodeKeyContent;
            std::shared_ptr<bytes> decrypted;
            try
            {
                if (nullptr == m_dataEncrypt)  // storage_security.enable = false
                {
                    enSmNodeKeyContent =
                        readContents(boost::filesystem::path(_nodeConfig->enSmNodeKey()));
                }
                else
                {
                    decrypted = m_dataEncrypt->decryptFile(_nodeConfig->enSmNodeKey());
                    if (decrypted)
                    {
                        enSmNodeKeyContent = std::move(*decrypted);
                    }
                }
            }
            catch (std::exception& e)
            {
                BCOS_LOG(ERROR) << LOG_BADGE("RpcFactory") << LOG_DESC("open enSmNodeKey failed")
                                << LOG_KV("file", _nodeConfig->nodeKey());
                BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                          "RpcFactory::initConfig: unable read content of key:" +
                                          _nodeConfig->nodeKey()));
            }
            // Reject OUTSIDE the try: see the nodeKey block above.
            if (m_dataEncrypt && !decrypted)
            {
                BOOST_THROW_EXCEPTION(
                    InvalidParameter() << errinfo_comment(
                        "RpcFactory::initConfig: decryptFile returned no content for key:" +
                        _nodeConfig->enSmNodeKey()));
            }
            if (enSmNodeKeyContent.empty())
            {
                BOOST_THROW_EXCEPTION(
                    InvalidParameter()
                    << errinfo_comment("RpcFactory::initConfig: unable read content of key:" +
                                       _nodeConfig->enSmNodeKey()));
            }
            certConfig.enNodeKey.resize(enSmNodeKeyContent.size());
            memcpy(certConfig.enNodeKey.data(), enSmNodeKeyContent.data(),
                enSmNodeKeyContent.size());
        }

        contextConfig->setIsCertPath(false);

        contextConfig->setSmCertConfig(certConfig);
        contextConfig->setSslType("sm_ssl");

        RPC_LOG(INFO) << LOG_DESC("rpc work in sm ssl model")
                      << LOG_KV("listenIP", wsConfig->listenIP())
                      << LOG_KV("listenPort", wsConfig->listenPort())
                      << LOG_KV("ioThreadCount", _nodeConfig->ioThreadCount())
                      << LOG_KV("asServer", wsConfig->asServer())
                      << LOG_KV("caCert", _nodeConfig->smCaCert())
                      << LOG_KV("nodeCert", _nodeConfig->smNodeCert())
                      << LOG_KV("nodeKey", _nodeConfig->smNodeKey())
                      << LOG_KV("enNodeCert", _nodeConfig->enSmNodeCert())
                      << LOG_KV("enNodeKey", _nodeConfig->enSmNodeKey());
    }

    wsConfig->setContextConfig(*contextConfig);

    return wsConfig;
}

// Init HTTP RPC service configuration. When _enableOPEngine=true, reads from
// [op_engine_rpc] section (OP-Stack Engine API); otherwise reads from [web3_rpc].
std::shared_ptr<bcos::boostssl::ws::WsConfig> RpcFactory::initWeb3RpcServiceConfig(
    const bcos::tool::NodeConfig::Ptr& _nodeConfig, bool _enableOPEngine)
{
    auto wsConfig = std::make_shared<boostssl::ws::WsConfig>();
    wsConfig->setModel(bcos::boostssl::ws::WsModel::Server);
    wsConfig->setDisableSsl(true);

    if (_enableOPEngine)
    {
        wsConfig->setListenIP(_nodeConfig->opEngineRpcListenIP());
        wsConfig->setListenPort(_nodeConfig->opEngineRpcListenPort());
        wsConfig->setMaxMsgSize(_nodeConfig->opEngineHttpBodySizeLimit());
        // The engine API port is machine-to-machine (op-node / consensus clients),
        // not browser-facing, so CORS provides no functionality and would only
        // expose the port to cross-origin pages. Disable it explicitly.
        wsConfig->setCorsConfig(bcos::boostssl::http::CorsConfig{.enableCORS = false});
        // The engine API is JSON-RPC over HTTP only, no websocket transport is
        // supported. Disable WS so Upgrade: websocket requests are rejected.
        wsConfig->setEnableWebSocket(false);
    }
    else
    {
        wsConfig->setListenIP(_nodeConfig->web3RpcListenIP());
        wsConfig->setListenPort(_nodeConfig->web3RpcListenPort());
        wsConfig->setMaxMsgSize(_nodeConfig->web3HttpBodySizeLimit());
        wsConfig->setCorsConfig(
            bcos::boostssl::http::CorsConfig{.enableCORS = _nodeConfig->web3EnableCors(),
                .allowCredentials = _nodeConfig->web3CorsAllowCredentials(),
                .allowedOrigins = _nodeConfig->web3CorsAllowedOrigins(),
                .allowedMethods = _nodeConfig->web3CorsAllowedMethods(),
                .allowedHeaders = _nodeConfig->web3CorsAllowedHeaders(),
                .maxAge = _nodeConfig->web3CorsMaxAge()});
    }
    RPC_LOG(INFO) << LOG_BADGE("initWeb3RpcServiceConfig")
                  << LOG_KV("listenIP", wsConfig->listenIP())
                  << LOG_KV("listenPort", wsConfig->listenPort())
                  << LOG_KV("ioThreadCount", _nodeConfig->ioThreadCount())
                  << LOG_KV("asServer", wsConfig->asServer())
                  << LOG_KV("maxMsgSize", wsConfig->maxMsgSize())
                  << LOG_KV("corsConfig", wsConfig->corsConfig().toString())
                  << LOG_KV("enableOPEngine", _enableOPEngine);

    return wsConfig;
}

bcos::boostssl::ws::WsService::Ptr RpcFactory::buildWsService(
    bcos::boostssl::ws::WsConfig::Ptr _config)
{
    auto wsService = std::make_shared<bcos::boostssl::ws::WsService>();
    auto initializer = std::make_shared<bcos::boostssl::ws::WsInitializer>();

    initializer->setConfig(std::move(_config));

    if (!m_ioServicePool)
    {
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment("No shared IOServicePool available for RPC!"));
    }
    // Use the shared external IOServicePool
    initializer->setIOServicePool(m_ioServicePool);
    initializer->initWsService(wsService);

    // Use shared IOServicePool's io_context for TimerFactory to avoid
    // creating dedicated "timerFactory" threads
    auto timerFactory = std::make_shared<timer::TimerFactory>(*m_ioServicePool->getIOService());
    wsService->setTimerFactory(std::move(timerFactory));

    return wsService;
}

bcos::rpc::JsonRpcImpl_2_0::Ptr RpcFactory::buildJsonRpc(int sendTxTimeout,
    const std::shared_ptr<boostssl::ws::WsService>& _wsService, GroupManager::Ptr _groupManager)
{
    // JsonRpcImpl_2_0
    auto filterSystem = std::make_shared<JsonRpcFilterSystem>(*m_ioServicePool->getIOService(),
        _groupManager, m_nodeConfig->groupId(), m_nodeConfig->rpcFilterTimeout(),
        m_nodeConfig->rpcMaxProcessBlock());
    auto jsonRpcInterface = std::make_shared<bcos::rpc::JsonRpcImpl_2_0>(
        _groupManager, m_gateway, _wsService, filterSystem, m_nodeConfig->forceSender());
    jsonRpcInterface->setSendTxTimeout(sendTxTimeout);

    if (auto httpServer = _wsService->httpServer())
    {
        httpServer->setHttpReqHandler(
            [jsonRpcInterface](const bcos::boostssl::http::HttpRequest& req, auto sender) {
                jsonRpcInterface->onRPCRequest(req.body(), std::move(sender));
            });
    }
    return jsonRpcInterface;
}

bcos::rpc::Web3JsonRpcImpl::Ptr RpcFactory::buildWeb3JsonRpc(int sendTxTimeout,
    boostssl::ws::WsService::Ptr _wsService, GroupManager::Ptr _groupManager, bool _enableOPEngine)
{
    // Each RPC surface (web3 / op-engine) gets its own FilterSystem so that
    // filter stores are isolated across ports (filters created on one port
    // cannot be removed from the other), and the internal static RNG used by
    // FilterSystem::insertFilter is not shared concurrently.
    auto filterSystem = std::make_shared<Web3FilterSystem>(*m_ioServicePool->getIOService(),
        _groupManager, m_nodeConfig->groupId(), m_nodeConfig->web3FilterTimeout(),
        m_nodeConfig->web3MaxProcessBlock());

    auto web3JsonRpc = std::make_shared<Web3JsonRpcImpl>(m_nodeConfig->groupId(),
        _enableOPEngine ? m_nodeConfig->opEngineBatchRequestSizeLimit() :
                          m_nodeConfig->web3BatchRequestSizeLimit(),
        std::move(_groupManager), std::move(filterSystem), m_nodeConfig->web3SyncTransaction(),
        _enableOPEngine);

    // if enable op engine, set jwt verifier and register op engine json http request handler
    if (_enableOPEngine)
    {
        auto jwtConfig = std::make_shared<bcos::rpc::JwtConfig>();
        jwtConfig->setSecretFile(m_nodeConfig->opEngineJwtSecretFile());
        jwtConfig->setClockSkewSecs(m_nodeConfig->opEngineClockSkewSecs());
        jwtConfig->setAllowedAlgorithms("HS256");
        web3JsonRpc->setJwtVerifier(std::make_shared<bcos::rpc::JwtVerifier>(std::move(jwtConfig)));
        if (auto httpServer = _wsService->httpServer())
        {
            httpServer->setHttpReqHandler(
                [web3JsonRpc](const bcos::boostssl::http::HttpRequest& req, auto sender) {
                    web3JsonRpc->onRPCRequest(req, std::move(sender));
                });
        }
        return web3JsonRpc;
    }
    else
    {
        // register web3 json http request handler
        if (auto httpServer = _wsService->httpServer())
        {
            httpServer->setHttpReqHandler(
                [web3JsonRpc](const bcos::boostssl::http::HttpRequest& req, auto sender) {
                    web3JsonRpc->onRPCRequest(req.body(), std::move(sender));
                });
        }

        // register web3 json websocket message handler
        _wsService->registerMsgHandler(
            WS_RAW_MESSAGE_TYPE, [web3JsonRpc](bcos::boostssl::ws::WsMessage msg,
                                     std::shared_ptr<bcos::boostssl::ws::WsSession> session) {
                auto payload = msg.payload();
                std::string_view strRequest((char*)payload.data(), payload.size());

                // RPC_LOG(INFO) << "web3 websocket request" << LOG_KV("request", strRequest);

                // the response message is constructed inside the async sender,
                // raw messages carry no header fields, nothing needs to be preserved
                web3JsonRpc->onRPCRequest(strRequest, session,
                    [session](bcos::bytes _respData, boost::beast::http::status) {
                        bcos::boostssl::ws::WsMessage respMsg(session->rawMessage());
                        respMsg.setPayload(bcos::bytes(std::move(_respData)));
                        session->asyncSendMessage(respMsg);
                    });
            });

        // web3 websocket connections use the raw wire format (payload only)
        _wsService->setRawMessage(true);
    }

    return web3JsonRpc;
}

bcos::event::EventSub::Ptr RpcFactory::buildEventSub(
    const std::shared_ptr<boostssl::ws::WsService>& _wsService, GroupManager::Ptr _groupManager)
{
    auto eventSubFactory = std::make_shared<event::EventSubFactory>();
    auto eventSub = eventSubFactory->buildEventSub(_wsService, *m_ioServicePool->getIOService());

    auto matcher = std::make_shared<event::EventSubMatcher>();
    eventSub->setGroupManager(std::move(_groupManager));
    eventSub->setMatcher(matcher);
    RPC_LOG(INFO) << LOG_DESC("create event sub obj");
    return eventSub;
}

Rpc::Ptr RpcFactory::buildRpc(std::string const& _gatewayServiceName,
    std::string const& _rpcServiceName, bcos::election::LeaderEntryPointInterface::Ptr _entryPoint)
{
    auto config = initConfig(m_nodeConfig);
    auto wsService = buildWsService(config);
    auto groupManager = buildGroupManager(_rpcServiceName, std::move(_entryPoint));
    auto amopClient = buildAMOPClient(wsService, _gatewayServiceName);

    RPC_LOG(INFO) << LOG_KV("listenIP", config->listenIP())
                  << LOG_KV("listenPort", config->listenPort())
                  << LOG_KV("ioThreadCount", m_nodeConfig->ioThreadCount())
                  << LOG_KV("gatewayServiceName", _gatewayServiceName);
    auto rpc = buildRpc(m_nodeConfig->sendTxTimeout(), wsService, groupManager, amopClient);
    return rpc;
}

Rpc::Ptr RpcFactory::buildLocalRpc(
    bcos::group::GroupInfo::Ptr _groupInfo, NodeService::Ptr _nodeService)
{
    auto config = initConfig(m_nodeConfig);
    auto wsService = buildWsService(config);
    auto groupManager = buildAirGroupManager(_groupInfo, _nodeService);
    auto amopClient = buildAirAMOPClient(wsService);
    auto rpc = buildRpc(m_nodeConfig->sendTxTimeout(), wsService, groupManager, amopClient);

    if (m_nodeConfig->enableOpEngineRpc())
    {
        auto opEngineConfig = initWeb3RpcServiceConfig(m_nodeConfig, true);
        auto opEngineWsService = buildWsService(std::move(opEngineConfig));
        // buildWeb3JsonRpc creates a dedicated FilterSystem for this port, so
        // filter stores are isolated between the OP Engine (8551) and web3 (8545).
        auto opEngineJsonRpc =
            buildWeb3JsonRpc(m_nodeConfig->sendTxTimeout(), opEngineWsService, groupManager, true);

        rpc->setOpEngineJsonRpcImpl(std::move(opEngineJsonRpc));
        rpc->setOpEngineService(std::move(opEngineWsService));
    }
    if (m_nodeConfig->enableWeb3Rpc())
    {
        auto web3Config = initWeb3RpcServiceConfig(m_nodeConfig);
        auto web3WsService = buildWsService(std::move(web3Config));

        auto web3JsonRpc =
            buildWeb3JsonRpc(m_nodeConfig->sendTxTimeout(), web3WsService, groupManager);

        auto weakPtrWeb3JsonRpc = std::weak_ptr<Web3JsonRpcImpl>(web3JsonRpc);

        auto web3Subscribe = std::make_shared<Web3Subscribe>(weakPtrWeb3JsonRpc);
        auto weakPtrWeb3Subscribe = std::weak_ptr<Web3Subscribe>(web3Subscribe);
        web3JsonRpc->setWeb3Subscribe(web3Subscribe);

        rpc->setWeb3Service(web3WsService);
        rpc->setWeb3JsonRpcImpl(std::move(web3JsonRpc));
        rpc->setWeb3Subscribe(web3Subscribe);
        // register for new block
        rpc->setOnNewBlock([weakPtrWeb3Subscribe](std::string const& _groupID,
                               bcos::protocol::BlockNumber _blockNumber) {
            auto web3Subscribe = weakPtrWeb3Subscribe.lock();
            if (web3Subscribe)
            {
                try
                {
                    web3Subscribe->onNewBlock(_blockNumber);
                }
                catch (std::exception& e)
                {
                    RPC_LOG(ERROR) << LOG_BADGE("setOnNewBlock") << LOG_DESC("onNewBlock exception")
                                   << LOG_KV("e", boost::diagnostic_information(e));
                }
            }
        });
        // register disconnect handler
        web3WsService->registerDisconnectHandler(
            [weakPtrWeb3Subscribe](std::shared_ptr<bcos::boostssl::ws::WsSession> _session) {
                auto web3Subscribe = weakPtrWeb3Subscribe.lock();
                if (web3Subscribe)
                {
                    try
                    {
                        web3Subscribe->onRemoveSubscribeBySession(std::move(_session));
                    }
                    catch (std::exception& e)
                    {
                        RPC_LOG(ERROR) << LOG_BADGE("registerDisconnectHandler")
                                       << LOG_DESC("onRemoveSubscribeBySession exception")
                                       << LOG_KV("e", boost::diagnostic_information(e));
                    }
                }
            });
    }
    // Note: init groupManager after create rpc and register the handlers
    groupManager->init();
    return rpc;
}

Rpc::Ptr RpcFactory::buildRpc(int sendTxTimeout,
    std::shared_ptr<boostssl::ws::WsService> _wsService, GroupManager::Ptr _groupManager,
    AMOPClient::Ptr _amopClient)
{
    // JsonRpc
    auto jsonRpc = buildJsonRpc(sendTxTimeout, _wsService, _groupManager);
    // EventSub
    auto es = buildEventSub(_wsService, _groupManager);
    return std::make_shared<Rpc>(_wsService, jsonRpc, es, _amopClient);
}

// Note: _rpcServiceName is used to check the validation of groupInfo when groupManager update
// groupInfo
GroupManager::Ptr RpcFactory::buildGroupManager(
    std::string const& _rpcServiceName, bcos::election::LeaderEntryPointInterface::Ptr _entryPoint)
{
    auto nodeServiceFactory = std::make_shared<NodeServiceFactory>();
    if (!_entryPoint)
    {
        RPC_LOG(INFO) << LOG_DESC("buildGroupManager: using tars to manager the node info");
        return std::make_shared<TarsGroupManager>(*m_ioServicePool->getIOService(), _rpcServiceName,
            m_chainID, nodeServiceFactory, m_nodeConfig);
    }
    RPC_LOG(INFO) << LOG_DESC("buildGroupManager with leaderEntryPoint to manager the node info");
    auto groupManager = std::make_shared<GroupManager>(
        _rpcServiceName, m_chainID, nodeServiceFactory, m_nodeConfig);
    auto groupInfoCodec = std::make_shared<bcostars::protocol::GroupInfoCodecImpl>();
    _entryPoint->addMemberChangeNotificationHandler(
        [groupManager, groupInfoCodec](
            std::string const& _key, bcos::protocol::MemberInterface::Ptr _member) {
            auto const& groupInfoStr = _member->memberConfig();
            auto groupInfo = groupInfoCodec->deserialize(groupInfoStr);
            groupManager->updateGroupInfo(groupInfo);
            RPC_LOG(INFO) << LOG_DESC("The leader entryPoint changed") << LOG_KV("key", _key)
                          << LOG_KV("memberID", _member->memberID())
                          << LOG_KV("modifyIndex", _member->seq())
                          << LOG_KV("groupID", groupInfo->groupID());
        });

    _entryPoint->addMemberDeleteNotificationHandler(
        [groupManager, groupInfoCodec](
            std::string const& _leaderKey, bcos::protocol::MemberInterface::Ptr _leader) {
            auto const& groupInfoStr = _leader->memberConfig();
            auto groupInfo = groupInfoCodec->deserialize(groupInfoStr);
            RPC_LOG(INFO) << LOG_DESC("The leader entryPoint has been deleted")
                          << LOG_KV("key", _leaderKey) << LOG_KV("memberID", _leader->memberID())
                          << LOG_KV("modifyIndex", _leader->seq())
                          << LOG_KV("groupID", groupInfo->groupID());
            groupManager->removeGroupNodeList(groupInfo);
        });
    return groupManager;
}

AirGroupManager::Ptr RpcFactory::buildAirGroupManager(
    GroupInfo::Ptr _groupInfo, NodeService::Ptr _nodeService)
{
    return std::make_shared<AirGroupManager>(m_chainID, _groupInfo, _nodeService);
}

AMOPClient::Ptr RpcFactory::buildAMOPClient(
    std::shared_ptr<boostssl::ws::WsService> _wsService, std::string const& _gatewayServiceName)
{
    auto requestFactory = std::make_shared<AMOPRequestFactory>();
    return std::make_shared<AMOPClient>(*m_ioServicePool->getIOService(), _wsService,
        requestFactory, m_gateway, _gatewayServiceName);
}

AMOPClient::Ptr RpcFactory::buildAirAMOPClient(std::shared_ptr<boostssl::ws::WsService> _wsService)
{
    auto requestFactory = std::make_shared<AMOPRequestFactory>();
    return std::make_shared<AirAMOPClient>(
        *m_ioServicePool->getIOService(), _wsService, requestFactory, m_gateway);
}