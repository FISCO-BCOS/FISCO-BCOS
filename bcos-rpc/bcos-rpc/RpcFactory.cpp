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

#include "interfaces/gateway/GatewayTypeDef.h"
#include <bcos-boostssl/context/ContextBuilder.h>
#include <bcos-boostssl/websocket/WsInitializer.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-framework/libprotocol/amop/AMOPRequest.h>
#include <bcos-framework/libutilities/Exceptions.h>
#include <bcos-framework/libutilities/FileUtility.h>
#include <bcos-framework/libutilities/Log.h>
#include <bcos-framework/libutilities/ThreadPool.h>
#include <bcos-rpc/RpcFactory.h>
#include <bcos-rpc/event/EventSubMatcher.h>
#include <bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h>
#include <bcos-rpc/ws/ProtocolVersion.h>
#include <boost/core/ignore_unused.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <memory>
#include <string>
#include <utility>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::gateway;
using namespace bcos::group;
using namespace bcos::boostssl::ws;
using namespace bcos::protocol;

RpcFactory::RpcFactory(std::string const& _chainID, GatewayInterface::Ptr _gatewayInterface,
    KeyFactory::Ptr _keyFactory)
  : m_chainID(_chainID), m_gateway(_gatewayInterface), m_keyFactory(_keyFactory)
{}

std::shared_ptr<bcos::boostssl::ws::WsConfig> RpcFactory::initConfig(
    bcos::tool::NodeConfig::Ptr _nodeConfig)
{
    auto wsConfig = std::make_shared<boostssl::ws::WsConfig>();
    wsConfig->setModel(bcos::boostssl::ws::WsModel::Server);

    wsConfig->setListenIP(_nodeConfig->rpcListenIP());
    wsConfig->setListenPort(_nodeConfig->rpcListenPort());
    wsConfig->setThreadPoolSize(_nodeConfig->rpcThreadPoolSize());
    wsConfig->setDisableSsl(_nodeConfig->rpcDisableSsl());
    if (_nodeConfig->rpcDisableSsl())
    {
        BCOS_LOG(INFO) << LOG_BADGE("[RPC][FACTORY][initConfig]")
                       << LOG_DESC("rpc work in disable ssl model")
                       << LOG_KV("listenIP", wsConfig->listenIP())
                       << LOG_KV("listenPort", wsConfig->listenPort())
                       << LOG_KV("threadCount", wsConfig->threadPoolSize())
                       << LOG_KV("asServer", wsConfig->asServer());
        return wsConfig;
    }

    auto contextConfig = std::make_shared<boostssl::context::ContextConfig>();
    if (!_nodeConfig->rpcSmSsl())
    {  //  ssl
        boostssl::context::ContextConfig::CertConfig certConfig;
        certConfig.caCert = _nodeConfig->caCert();
        certConfig.nodeCert = _nodeConfig->nodeCert();
        certConfig.nodeKey = _nodeConfig->nodeKey();
        contextConfig->setCertConfig(certConfig);
        contextConfig->setSslType("ssl");

        BCOS_LOG(INFO) << LOG_DESC("[RPC][FACTORY][initConfig]")
                       << LOG_DESC("rpc work in ssl model")
                       << LOG_KV("listenIP", wsConfig->listenIP())
                       << LOG_KV("listenPort", wsConfig->listenPort())
                       << LOG_KV("threadCount", wsConfig->threadPoolSize())
                       << LOG_KV("asServer", wsConfig->asServer())
                       << LOG_KV("caCert", _nodeConfig->caCert())
                       << LOG_KV("nodeCert", _nodeConfig->nodeCert())
                       << LOG_KV("nodeKey", _nodeConfig->nodeKey());
    }
    else
    {  // sm ssl
        boostssl::context::ContextConfig::SMCertConfig certConfig;
        certConfig.caCert = _nodeConfig->smCaCert();
        certConfig.nodeCert = _nodeConfig->smNodeCert();
        certConfig.nodeKey = _nodeConfig->smNodeKey();
        certConfig.enNodeCert = _nodeConfig->enSmNodeCert();
        certConfig.enNodeKey = _nodeConfig->enSmNodeKey();
        contextConfig->setSmCertConfig(certConfig);
        contextConfig->setSslType("sm_ssl");

        BCOS_LOG(INFO) << LOG_DESC("[RPC][FACTORY][initConfig]")
                       << LOG_DESC("rpc work in sm ssl model")
                       << LOG_KV("listenIP", wsConfig->listenIP())
                       << LOG_KV("listenPort", wsConfig->listenPort())
                       << LOG_KV("threadCount", wsConfig->threadPoolSize())
                       << LOG_KV("asServer", wsConfig->asServer())
                       << LOG_KV("caCert", _nodeConfig->smCaCert())
                       << LOG_KV("nodeCert", _nodeConfig->smNodeCert())
                       << LOG_KV("nodeKey", _nodeConfig->smNodeKey())
                       << LOG_KV("enNodeCert", _nodeConfig->enSmNodeCert())
                       << LOG_KV("enNodeKey", _nodeConfig->enSmNodeKey());
    }

    wsConfig->setContextConfig(contextConfig);

    return wsConfig;
}

bcos::boostssl::ws::WsService::Ptr RpcFactory::buildWsService(
    bcos::boostssl::ws::WsConfig::Ptr _config)
{
    auto wsService = std::make_shared<bcos::boostssl::ws::WsService>();
    auto initializer = std::make_shared<bcos::boostssl::ws::WsInitializer>();

    initializer->setConfig(_config);
    initializer->initWsService(wsService);

    return wsService;
}

void RpcFactory::registerHandlers(std::shared_ptr<boostssl::ws::WsService> _wsService,
    bcos::rpc::JsonRpcImpl_2_0::Ptr _jsonRpcInterface)
{
    _wsService->registerMsgHandler(bcos::rpc::MessageType::HANDESHAKE,
        [_jsonRpcInterface](std::shared_ptr<boostssl::ws::WsMessage> _msg,
            std::shared_ptr<boostssl::ws::WsSession> _session) {
            auto seq = std::string(_msg->data()->begin(), _msg->data()->end());
            auto version = ws::EnumPV::CurrentVersion;
            _session->setVersion(version);

            _jsonRpcInterface->getGroupInfoList(
                [_msg, _session, version, seq](
                    bcos::Error::Ptr _error, Json::Value& _jGroupInfoList) {
                    if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
                    {
                        BCOS_LOG(ERROR)
                            << LOG_BADGE("HANDSHAKE") << LOG_DESC("get group info list error")
                            << LOG_KV("seq", seq)
                            << LOG_KV("endpoint", _session ? _session->endPoint() : std::string(""))
                            << LOG_KV("errorCode", _error->errorCode())
                            << LOG_KV("errorMessage", _error->errorMessage());
                        return;
                    }

                    auto pv = std::make_shared<ws::ProtocolVersion>();
                    pv->setProtocolVersion(version);
                    auto jResult = pv->toJson();
                    jResult["groupInfoList"] = _jGroupInfoList;

                    Json::FastWriter writer;
                    std::string result = writer.write(jResult);

                    _msg->setData(std::make_shared<bcos::bytes>(result.begin(), result.end()));
                    _session->asyncSendMessage(_msg);

                    BCOS_LOG(INFO)
                        << LOG_BADGE("HANDSHAKE") << LOG_DESC("handshake response")
                        << LOG_KV("version", version) << LOG_KV("seq", seq)
                        << LOG_KV("endpoint", _session ? _session->endPoint() : std::string(""))
                        << LOG_KV("result", result);
                });
        });

    _wsService->registerMsgHandler(bcos::rpc::MessageType::RPC_REQUEST,
        [_jsonRpcInterface](std::shared_ptr<boostssl::ws::WsMessage> _msg,
            std::shared_ptr<boostssl::ws::WsSession> _session) {
            if (!_jsonRpcInterface)
            {
                return;
            }
            std::string req = std::string(_msg->data()->begin(), _msg->data()->end());
            _jsonRpcInterface->onRPCRequest(req, [req, _msg, _session](const std::string& _resp) {
                if (_session && _session->isConnected())
                {
                    auto buffer = std::make_shared<bcos::bytes>(_resp.begin(), _resp.end());
                    _msg->setData(buffer);
                    _session->asyncSendMessage(_msg);
                }
                else
                {
                    auto seq = std::string(_msg->seq()->begin(), _msg->seq()->end());
                    BCOS_LOG(WARNING)
                        << LOG_DESC("[RPC][FACTORY][buildJsonRpc]")
                        << LOG_DESC("unable to send response for session has been inactive")
                        << LOG_KV("req", req) << LOG_KV("resp", _resp) << LOG_KV("seq", seq)
                        << LOG_KV("endpoint", _session ? _session->endPoint() : std::string(""));
                }
            });
        });
}
bcos::rpc::JsonRpcImpl_2_0::Ptr RpcFactory::buildJsonRpc(
    std::shared_ptr<boostssl::ws::WsService> _wsService, GroupManager::Ptr _groupManager)
{
    // JsonRpcImpl_2_0
    auto jsonRpcInterface = std::make_shared<bcos::rpc::JsonRpcImpl_2_0>(_groupManager, m_gateway);
    auto httpServer = _wsService->httpServer();
    if (httpServer)
    {
        httpServer->setHttpReqHandler(std::bind(&bcos::rpc::JsonRpcInterface::onRPCRequest,
            jsonRpcInterface, std::placeholders::_1, std::placeholders::_2));
    }
    registerHandlers(_wsService, jsonRpcInterface);
    return jsonRpcInterface;
}

bcos::event::EventSub::Ptr RpcFactory::buildEventSub(
    std::shared_ptr<boostssl::ws::WsService> _wsService, GroupManager::Ptr _groupManager)
{
    auto eventSubFactory = std::make_shared<event::EventSubFactory>();
    auto eventSub = eventSubFactory->buildEventSub();

    auto matcher = std::make_shared<event::EventSubMatcher>();
    eventSub->setIoc(_wsService->ioc());
    eventSub->setGroupManager(_groupManager);
    eventSub->setMessageFactory(_wsService->messageFactory());
    eventSub->setMatcher(matcher);

    auto eventSubWeakPtr = std::weak_ptr<bcos::event::EventSub>(eventSub);

    // register event subscribe message
    _wsService->registerMsgHandler(bcos::event::MessageType::EVENT_SUBSCRIBE,
        [eventSubWeakPtr](std::shared_ptr<WsMessage> _msg, std::shared_ptr<WsSession> _session) {
            auto eventSub = eventSubWeakPtr.lock();
            if (eventSub)
            {
                eventSub->onRecvSubscribeEvent(_msg, _session);
            }
        });

    // register event subscribe message
    _wsService->registerMsgHandler(bcos::event::MessageType::EVENT_UNSUBSCRIBE,
        [eventSubWeakPtr](std::shared_ptr<WsMessage> _msg, std::shared_ptr<WsSession> _session) {
            auto eventSub = eventSubWeakPtr.lock();
            if (eventSub)
            {
                eventSub->onRecvUnsubscribeEvent(_msg, _session);
            }
        });

    BCOS_LOG(INFO) << LOG_DESC("[RPC][FACTORY][buildEventSub]") << LOG_DESC("create event sub obj");

    return eventSub;
}

Rpc::Ptr RpcFactory::buildRpc(std::string const& _gatewayServiceName)
{
    auto config = initConfig(m_nodeConfig);
    auto wsService = buildWsService(config);
    auto groupManager = buildGroupManager();
    auto amopClient = buildAMOPClient(wsService, _gatewayServiceName);

    BCOS_LOG(INFO) << LOG_DESC("[RPC][FACTORY][buildRpc]") << LOG_KV("listenIP", config->listenIP())
                   << LOG_KV("listenPort", config->listenPort())
                   << LOG_KV("threadCount", config->threadPoolSize())
                   << LOG_KV("gatewayServiceName", _gatewayServiceName);
    auto rpc = buildRpc(wsService, groupManager, amopClient);
    return rpc;
}

Rpc::Ptr RpcFactory::buildLocalRpc(
    bcos::group::GroupInfo::Ptr _groupInfo, NodeService::Ptr _nodeService)
{
    auto config = initConfig(m_nodeConfig);
    auto wsService = buildWsService(config);
    auto groupManager = buildLocalGroupManager(_groupInfo, _nodeService);
    auto amopClient = buildLocalAMOPClient(wsService);
    return buildRpc(wsService, groupManager, amopClient);
}

/**
 * @brief: Rpc
 * @param _config: WsConfig
 * @param _nodeInfo: node info
 * @return Rpc::Ptr:
 */
Rpc::Ptr RpcFactory::buildRpc(std::shared_ptr<boostssl::ws::WsService> _wsService,
    GroupManager::Ptr _groupManager, AMOPClient::Ptr _amopClient)
{
    // JsonRpc
    auto jsonRpc = buildJsonRpc(_wsService, _groupManager);
    // EventSub
    auto es = buildEventSub(_wsService, _groupManager);
    return std::make_shared<Rpc>(_wsService, jsonRpc, es, _amopClient);
}

GroupManager::Ptr RpcFactory::buildGroupManager()
{
    auto nodeServiceFactory = std::make_shared<NodeServiceFactory>();
    return std::make_shared<GroupManager>(m_chainID, nodeServiceFactory);
}

GroupManager::Ptr RpcFactory::buildLocalGroupManager(
    GroupInfo::Ptr _groupInfo, NodeService::Ptr _nodeService)
{
    return std::make_shared<LocalGroupManager>(m_chainID, _groupInfo, _nodeService);
}

AMOPClient::Ptr RpcFactory::buildAMOPClient(
    std::shared_ptr<boostssl::ws::WsService> _wsService, std::string const& _gatewayServiceName)
{
    auto wsFactory = std::make_shared<WsMessageFactory>();
    auto requestFactory = std::make_shared<AMOPRequestFactory>();
    return std::make_shared<AMOPClient>(
        _wsService, wsFactory, requestFactory, m_gateway, _gatewayServiceName);
}

AMOPClient::Ptr RpcFactory::buildLocalAMOPClient(
    std::shared_ptr<boostssl::ws::WsService> _wsService)
{
    auto wsFactory = std::make_shared<WsMessageFactory>();
    auto requestFactory = std::make_shared<AMOPRequestFactory>();
    return std::make_shared<LocalAMOPClient>(_wsService, wsFactory, requestFactory, m_gateway);
}