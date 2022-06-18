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

#include "bcos-framework//gateway/GatewayTypeDef.h"
#include "bcos-rpc/groupmgr/TarsGroupManager.h"
#include <bcos-boostssl/context/ContextBuilder.h>
#include <bcos-boostssl/websocket/WsError.h>
#include <bcos-boostssl/websocket/WsInitializer.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-framework//Common.h>
#include <bcos-framework//protocol/AMOPRequest.h>
#include <bcos-framework//security/DataEncryptInterface.h>
#include <bcos-rpc/RpcFactory.h>
#include <bcos-rpc/event/EventSubMatcher.h>
#include <bcos-rpc/jsonrpc/DupTestTxJsonRpcImpl_2_0.h>
#include <bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h>
#include <bcos-tars-protocol/protocol/GroupInfoCodecImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/FileUtility.h>
#include <bcos-utilities/ThreadPool.h>
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
using namespace bcos::security;

RpcFactory::RpcFactory(std::string const& _chainID, GatewayInterface::Ptr _gatewayInterface,
    KeyFactory::Ptr _keyFactory, bcos::security::DataEncryptInterface::Ptr _dataEncrypt)
  : m_chainID(_chainID),
    m_gateway(_gatewayInterface),
    m_keyFactory(_keyFactory),
    m_dataEncrypt(_dataEncrypt)
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
        RPC_LOG(INFO) << LOG_BADGE("initConfig") << LOG_DESC("rpc work in disable ssl model")
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

        std::shared_ptr<bytes> keyContent;

        // caCert
        if (false == _nodeConfig->caCert().empty())
        {
            try
            {
                keyContent = readContents(boost::filesystem::path(_nodeConfig->caCert()));
                if (nullptr != keyContent)
                {
                    certConfig.caCert.resize(keyContent->size());
                    memcpy(certConfig.caCert.data(), keyContent->data(), keyContent->size());
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
        if (false == _nodeConfig->nodeCert().empty())
        {
            try
            {
                keyContent = readContents(boost::filesystem::path(_nodeConfig->nodeCert()));
                if (nullptr != keyContent)
                {
                    certConfig.nodeCert.resize(keyContent->size());
                    memcpy(certConfig.nodeCert.data(), keyContent->data(), keyContent->size());
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
        if (false == _nodeConfig->nodeKey().empty())
        {
            try
            {
                if (nullptr == m_dataEncrypt)  // storage_security.enable = false
                    keyContent = readContents(boost::filesystem::path(_nodeConfig->nodeKey()));
                else
                    keyContent = m_dataEncrypt->decryptFile(_nodeConfig->nodeKey());
            }
            catch (std::exception& e)
            {
                BCOS_LOG(ERROR) << LOG_BADGE("RpcFactory") << LOG_DESC("open nodeKey failed")
                                << LOG_KV("file", _nodeConfig->nodeKey());
                BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                          "RpcFactory::initConfig: unable read content of key:" +
                                          _nodeConfig->nodeKey()));
            }
        }
        certConfig.nodeKey.resize(keyContent->size());
        memcpy(certConfig.nodeKey.data(), keyContent->data(), keyContent->size());

        contextConfig->setIsCertPath(false);

        contextConfig->setCertConfig(certConfig);
        contextConfig->setSslType("ssl");

        RPC_LOG(INFO) << LOG_DESC("rpc work in ssl model")
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

        std::shared_ptr<bytes> keyContent;

        // caCert
        if (false == _nodeConfig->smCaCert().empty())
        {
            try
            {
                keyContent = readContents(boost::filesystem::path(_nodeConfig->smCaCert()));
                if (nullptr != keyContent)
                {
                    certConfig.caCert.resize(keyContent->size());
                    memcpy(certConfig.caCert.data(), keyContent->data(), keyContent->size());
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
        if (false == _nodeConfig->smNodeCert().empty())
        {
            try
            {
                keyContent = readContents(boost::filesystem::path(_nodeConfig->smNodeCert()));
                if (nullptr != keyContent)
                {
                    certConfig.nodeCert.resize(keyContent->size());
                    memcpy(certConfig.nodeCert.data(), keyContent->data(), keyContent->size());
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
        if (false == _nodeConfig->smNodeKey().empty())
        {
            try
            {
                if (nullptr == m_dataEncrypt)  // storage_security.enable = false
                    keyContent = readContents(boost::filesystem::path(_nodeConfig->smNodeKey()));
                else
                    keyContent = m_dataEncrypt->decryptFile(_nodeConfig->smNodeKey());
            }
            catch (std::exception& e)
            {
                BCOS_LOG(ERROR) << LOG_BADGE("RpcFactory") << LOG_DESC("open smNodeKey failed")
                                << LOG_KV("file", _nodeConfig->nodeKey());
                BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                          "RpcFactory::initConfig: unable read content of key:" +
                                          _nodeConfig->nodeKey()));
            }
        }
        certConfig.nodeKey.resize(keyContent->size());
        memcpy(certConfig.nodeKey.data(), keyContent->data(), keyContent->size());

        // enNodeCert
        if (false == _nodeConfig->enSmNodeCert().empty())
        {
            try
            {
                keyContent = readContents(boost::filesystem::path(_nodeConfig->enSmNodeCert()));
                if (nullptr != keyContent)
                {
                    certConfig.enNodeCert.resize(keyContent->size());
                    memcpy(certConfig.enNodeCert.data(), keyContent->data(), keyContent->size());
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
        if (false == _nodeConfig->enSmNodeKey().empty())
        {
            try
            {
                if (nullptr == m_dataEncrypt)  // storage_security.enable = false
                    keyContent = readContents(boost::filesystem::path(_nodeConfig->enSmNodeKey()));
                else
                    keyContent = m_dataEncrypt->decryptFile(_nodeConfig->enSmNodeKey());
            }
            catch (std::exception& e)
            {
                BCOS_LOG(ERROR) << LOG_BADGE("RpcFactory") << LOG_DESC("open enSmNodeKey failed")
                                << LOG_KV("file", _nodeConfig->nodeKey());
                BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                          "RpcFactory::initConfig: unable read content of key:" +
                                          _nodeConfig->nodeKey()));
            }
        }
        certConfig.enNodeKey.resize(keyContent->size());
        memcpy(certConfig.enNodeKey.data(), keyContent->data(), keyContent->size());

        contextConfig->setIsCertPath(false);

        contextConfig->setSmCertConfig(certConfig);
        contextConfig->setSslType("sm_ssl");

        RPC_LOG(INFO) << LOG_DESC("rpc work in sm ssl model")
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

bcos::rpc::JsonRpcImpl_2_0::Ptr RpcFactory::buildJsonRpc(
    std::shared_ptr<boostssl::ws::WsService> _wsService, GroupManager::Ptr _groupManager)
{
    // JsonRpcImpl_2_0
    //*
    auto jsonRpcInterface =
        std::make_shared<bcos::rpc::JsonRpcImpl_2_0>(_groupManager, m_gateway, _wsService);
    /*/
        auto jsonRpcInterface =
            std::make_shared<bcos::rpc::DupTestTxJsonRpcImpl_2_0>(_groupManager, m_gateway,
       _wsService);
        //*/
    auto httpServer = _wsService->httpServer();
    if (httpServer)
    {
        httpServer->setHttpReqHandler(std::bind(&bcos::rpc::JsonRpcInterface::onRPCRequest,
            jsonRpcInterface, std::placeholders::_1, std::placeholders::_2));
    }
    return jsonRpcInterface;
}

bcos::event::EventSub::Ptr RpcFactory::buildEventSub(
    std::shared_ptr<boostssl::ws::WsService> _wsService, GroupManager::Ptr _groupManager)
{
    auto eventSubFactory = std::make_shared<event::EventSubFactory>();
    auto eventSub = eventSubFactory->buildEventSub(_wsService);

    auto matcher = std::make_shared<event::EventSubMatcher>();
    eventSub->setGroupManager(_groupManager);
    eventSub->setMessageFactory(_wsService->messageFactory());
    eventSub->setMatcher(matcher);
    RPC_LOG(INFO) << LOG_DESC("create event sub obj");
    return eventSub;
}

Rpc::Ptr RpcFactory::buildRpc(std::string const& _gatewayServiceName,
    std::string const& _rpcServiceName, bcos::election::LeaderEntryPointInterface::Ptr _entryPoint)
{
    auto config = initConfig(m_nodeConfig);
    auto wsService = buildWsService(config);
    auto groupManager = buildGroupManager(_rpcServiceName, _entryPoint);
    auto amopClient = buildAMOPClient(wsService, _gatewayServiceName);

    RPC_LOG(INFO) << LOG_KV("listenIP", config->listenIP())
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
    auto groupManager = buildAirGroupManager(_groupInfo, _nodeService);
    auto amopClient = buildAirAMOPClient(wsService);
    auto rpc = buildRpc(wsService, groupManager, amopClient);
    // Note: init groupManager after create rpc and register the handlers
    groupManager->init();
    return rpc;
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

// Note: _rpcServiceName is used to check the validation of groupInfo when groupManager update
// groupInfo
GroupManager::Ptr RpcFactory::buildGroupManager(
    std::string const& _rpcServiceName, bcos::election::LeaderEntryPointInterface::Ptr _entryPoint)
{
    auto nodeServiceFactory = std::make_shared<NodeServiceFactory>();
    if (!_entryPoint)
    {
        RPC_LOG(INFO) << LOG_DESC("buildGroupManager: using tars to manager the node info");
        return std::make_shared<TarsGroupManager>(_rpcServiceName, m_chainID, nodeServiceFactory);
    }
    RPC_LOG(INFO) << LOG_DESC("buildGroupManager with leaderEntryPoint to manager the node info");
    auto groupManager =
        std::make_shared<GroupManager>(_rpcServiceName, m_chainID, nodeServiceFactory);
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
    auto wsFactory = std::make_shared<WsMessageFactory>();
    auto requestFactory = std::make_shared<AMOPRequestFactory>();
    return std::make_shared<AMOPClient>(
        _wsService, wsFactory, requestFactory, m_gateway, _gatewayServiceName);
}

AMOPClient::Ptr RpcFactory::buildAirAMOPClient(std::shared_ptr<boostssl::ws::WsService> _wsService)
{
    auto wsFactory = std::make_shared<WsMessageFactory>();
    auto requestFactory = std::make_shared<AMOPRequestFactory>();
    return std::make_shared<AirAMOPClient>(_wsService, wsFactory, requestFactory, m_gateway);
}