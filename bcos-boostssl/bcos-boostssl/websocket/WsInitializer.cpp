/*
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
 * @file WsFactory.cpp
 * @author: octopus
 * @date 2021-09-29
 */
#include <bcos-boostssl/context/ContextBuilder.h>
#include <bcos-boostssl/context/NodeInfoTools.h>
#include <bcos-boostssl/httpserver/Common.h>
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsConnector.h>
#include <bcos-boostssl/websocket/WsInitializer.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-boostssl/websocket/WsTools.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/IOServicePool.h>
#include <bcos-utilities/ThreadPool.h>
#include <cstddef>
#include <memory>

using namespace bcos;
using namespace bcos::boostssl;
using namespace bcos::boostssl::context;
using namespace bcos::boostssl::ws;
using namespace bcos::boostssl::http;

void WsInitializer::initWsService(WsService::Ptr _wsService)
{
    std::shared_ptr<WsConfig> _config = m_config;
    std::string m_moduleName = _config->moduleName();
    auto messageFactory = m_messageFactory;
    if (!messageFactory)
    {
        messageFactory = std::make_shared<WsMessageFactory>();
    }

    auto sessionFactory = m_sessionFactory;
    if (!sessionFactory)
    {
        sessionFactory = std::make_shared<WsSessionFactory>();
    }

    auto threadPoolSize = _config->threadPoolSize() > 0 ? _config->threadPoolSize() :
                                                          std::thread::hardware_concurrency();
    if (!threadPoolSize)
    {
        threadPoolSize = 16;
    }
    auto wsServiceWeakPtr = std::weak_ptr<WsService>(_wsService);
    auto ioServicePool = std::make_shared<IOServicePool>();
    _wsService->setIOServicePool(ioServicePool);

    auto resolver =
        std::make_shared<boost::asio::ip::tcp::resolver>((*(ioServicePool->getIOService())));
    auto connector = std::make_shared<WsConnector>(resolver);
    connector->setIOServicePool(ioServicePool);

    auto builder = std::make_shared<WsStreamDelegateBuilder>();
    auto threadPool = std::make_shared<ThreadPool>("t_ws_pool", threadPoolSize);

    // init module_name for log
    WsTools::setModuleName(m_moduleName);
    NodeInfoTools::setModuleName(m_moduleName);
    connector->setModuleName(m_moduleName);

    std::shared_ptr<boost::asio::ssl::context> srvCtx = nullptr;
    std::shared_ptr<boost::asio::ssl::context> clientCtx = nullptr;
    if (!_config->disableSsl())
    {
        auto contextBuilder = std::make_shared<ContextBuilder>();

        // init module_name for log
        contextBuilder->setModuleName(m_moduleName);
        _config->contextConfig()->setModuleName(m_moduleName);

        srvCtx = contextBuilder->buildSslContext(true, *_config->contextConfig());
        clientCtx = contextBuilder->buildSslContext(false, *_config->contextConfig());
    }

    if (_config->asServer())
    {
        WEBSOCKET_INITIALIZER(INFO)
            << LOG_BADGE("initWsService") << LOG_DESC("start websocket service as server");

        if (!WsTools::validIP(_config->listenIP()))
        {
            BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                      "invalid listen ip, value: " + _config->listenIP()));
        }

        if (!WsTools::validPort(_config->listenPort()))
        {
            BOOST_THROW_EXCEPTION(
                InvalidParameter() << errinfo_comment(
                    "invalid listen port, value: " + std::to_string(_config->listenPort())));
        }

        auto httpServerFactory = std::make_shared<HttpServerFactory>();
        auto httpServer = httpServerFactory->buildHttpServer(_config->listenIP(),
            _config->listenPort(), ioServicePool->getIOService(), srvCtx, m_moduleName);
        httpServer->setIOServicePool(ioServicePool);
        httpServer->setDisableSsl(_config->disableSsl());
        httpServer->setThreadPool(threadPool);
        httpServer->setWsUpgradeHandler(
            [wsServiceWeakPtr](std::shared_ptr<HttpStream> _httpStream, HttpRequest&& _httpRequest,
                std::shared_ptr<std::string> _nodeId) {
                auto service = wsServiceWeakPtr.lock();
                if (service)
                {
                    std::string nodeIdString = _nodeId == nullptr ? "" : *_nodeId.get();
                    auto session = service->newSession(_httpStream->wsStream(), nodeIdString);
                    session->startAsServer(_httpRequest);
                }
            });

        _wsService->setHttpServer(httpServer);
        _wsService->setHostPort(_config->listenIP(), _config->listenPort());
    }

    if (_config->asClient())
    {
        auto connectPeers = _config->connectPeers();
        WEBSOCKET_INITIALIZER(INFO)
            << LOG_BADGE("initWsService") << LOG_DESC("start websocket service as client")
            << LOG_KV("connected size", connectPeers ? connectPeers->size() : 0);

        if (connectPeers)
        {
            for (auto& peer : *connectPeers)
            {
                if (!WsTools::validIP(peer.address()))
                {
                    BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                              "invalid connected peer, value: " + peer.address()));
                }

                if (!WsTools::validPort(peer.port()))
                {
                    BOOST_THROW_EXCEPTION(
                        InvalidParameter() << errinfo_comment(
                            "invalid connect port, value: " + std::to_string(peer.port())));
                }
            }

            // connectPeers info is valid then set connectPeers info into wsService
            _wsService->setReconnectedPeers(connectPeers);
        }
        else
        {
            WEBSOCKET_INITIALIZER(WARNING)
                << LOG_BADGE("initWsService") << LOG_DESC("there has no connected server config");
        }
    }

    connector->setCtx(clientCtx);
    connector->setBuilder(builder);

    _wsService->setConfig(_config);
    _wsService->setConnector(connector);
    _wsService->setThreadPool(threadPool);
    _wsService->setMessageFactory(messageFactory);
    _wsService->setSessionFactory(sessionFactory);

    WEBSOCKET_INITIALIZER(INFO)
        << LOG_BADGE("initWsService") << LOG_DESC("initializer for websocket service")
        << LOG_KV("listenIP", _config->listenIP()) << LOG_KV("listenPort", _config->listenPort())
        << LOG_KV("disableSsl", _config->disableSsl()) << LOG_KV("server", _config->asServer())
        << LOG_KV("client", _config->asClient())
        << LOG_KV("threadPoolSize", _config->threadPoolSize())
        << LOG_KV("maxMsgSize", _config->maxMsgSize())
        << LOG_KV("msgTimeOut", _config->sendMsgTimeout())
        << LOG_KV("connected peers", _config->connectPeers() ? _config->connectPeers()->size() : 0);
}
