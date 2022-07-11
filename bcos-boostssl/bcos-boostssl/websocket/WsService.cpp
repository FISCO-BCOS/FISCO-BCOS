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
 * @file WsService.cpp
 * @author: octopus
 * @date 2021-07-28
 */
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsError.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreadPool.h>
#include <json/json.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using namespace bcos;
using namespace std::chrono_literals;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;

WsService::WsService(std::string _moduleName) : m_moduleName(_moduleName)
{
    WEBSOCKET_SERVICE(INFO) << LOG_KV("[NEWOBJ][WsService]", this);
}

WsService::~WsService()
{
    stop();
    WEBSOCKET_SERVICE(INFO) << LOG_KV("[DELOBJ][WsService]", this);
}

void WsService::start()
{
    if (m_running)
    {
        WEBSOCKET_SERVICE(INFO) << LOG_BADGE("start") << LOG_DESC("websocket service is running");
        return;
    }
    m_running = true;

    // start ioc thread
    if (m_ioservicePool)
    {
        m_ioservicePool->start();
    }

    // start as server
    if (m_config->asServer())
    {
        m_httpServer->start();
    }

    // start as client
    if (m_config->asClient())
    {
        if (m_config->connectPeers() && !m_config->connectPeers()->empty())
        {
            // Connect to peers and wait for at least one connection to be successfully
            // established
            syncConnectToEndpoints(m_config->connectPeers());
        }

        reconnect();
    }

    reportConnectedNodes();

    WEBSOCKET_SERVICE(INFO) << LOG_BADGE("start")
                            << LOG_DESC("start websocket service successfully")
                            << LOG_KV("model", m_config->model())
                            << LOG_KV("max msg size", m_config->maxMsgSize());
}

void WsService::stop()
{
    if (!m_running)
    {
        WEBSOCKET_SERVICE(INFO) << LOG_BADGE("stop")
                                << LOG_DESC("websocket service has been stopped");
        return;
    }
    m_running = false;

    // stop ioc thread
    if (m_ioservicePool)
    {
        m_ioservicePool->stop();
    }

    // cancel reconnect task
    if (m_reconnect)
    {
        m_reconnect->cancel();
    }

    // cancel heartbeat task
    if (m_heartbeat)
    {
        m_heartbeat->cancel();
    }



    WEBSOCKET_SERVICE(INFO) << LOG_BADGE("stop") << LOG_DESC("stop websocket service successfully");
}



void WsService::reportConnectedNodes()
{
    auto ss = sessions();
    WEBSOCKET_SERVICE(INFO) << LOG_DESC("connected nodes") << LOG_KV("count", ss.size());

    m_heartbeat = std::make_shared<boost::asio::deadline_timer>(
        *(m_timerIoc), boost::posix_time::milliseconds(m_config->heartbeatPeriod()));
    auto self = std::weak_ptr<WsService>(shared_from_this());
    m_heartbeat->async_wait([self](const boost::system::error_code& _error) {
        if (_error == boost::asio::error::operation_aborted)
        {
            return;
        }
        try
        {
            auto service = self.lock();
            if (!service)
            {
                return;
            }
            service->reportConnectedNodes();
        }
        catch (std::exception const& e)
        {
            BOOST_SSL_LOG(WARNING) << LOG_DESC("reportConnectedNodes exception")
                                   << LOG_KV("error", boost::diagnostic_information(e));
        }
    });
}

std::string WsService::genConnectError(
    const std::string& _error, const std::string& endpoint, bool end)
{
    std::string msg = _error;
    msg += ":/";
    msg += endpoint;
    if (!end)
    {
        msg += ", ";
    }
    return msg;
}

void WsService::syncConnectToEndpoints(EndPointsPtr _peers)
{
    std::string errorMsg;
    std::size_t sucCount = 0;

    auto vPromise = asyncConnectToEndpoints(_peers);

    for (std::size_t i = 0; i < vPromise->size(); ++i)
    {
        auto fut = (*vPromise)[i]->get_future();

        auto status = fut.wait_for(std::chrono::milliseconds(m_waitConnectFinishTimeout));
        auto [errCode, errMsg, endpoint] = fut.get();
        switch (status)
        {
        case std::future_status::deferred:
            break;
        case std::future_status::timeout:
            errorMsg += genConnectError("connection timeout", endpoint, i == vPromise->size() - 1);
            break;
        case std::future_status::ready:

            try
            {
                if (errCode)
                {
                    errorMsg += genConnectError(
                        errMsg.empty() ? errCode.message() : errMsg + " " + errCode.message(),
                        endpoint, i == vPromise->size() - 1);
                }
                else
                {
                    sucCount++;
                }
            }
            catch (std::exception& _e)
            {
                WEBSOCKET_SERVICE(WARNING)
                    << LOG_BADGE("syncConnectToEndpoints") << LOG_DESC("future get throw exception")
                    << LOG_KV("e", _e.what());
            }
            break;
        }
    }

    if (sucCount == 0)
    {
        stop();
        BOOST_THROW_EXCEPTION(std::runtime_error("[" + boost::to_lower_copy(errorMsg) + "]"));
        return;
    }
}

std::shared_ptr<std::vector<
    std::shared_ptr<std::promise<std::tuple<boost::beast::error_code, std::string, std::string>>>>>
WsService::asyncConnectToEndpoints(EndPointsPtr _peers)
{
    auto vPromise = std::make_shared<std::vector<std::shared_ptr<
        std::promise<std::tuple<boost::beast::error_code, std::string, std::string>>>>>();

    for (auto& peer : *_peers)
    {
        std::string connectedEndPoint = peer.address() + ":" + std::to_string(peer.port());

        /*
        WEBSOCKET_SERVICE(DEBUG) << LOG_BADGE("asyncConnect")
                                 << LOG_DESC("try to connect to endpoint")
                                 << LOG_KV("host", (peer.address())) << LOG_KV("port", peer.port());
        */

        auto p = std::make_shared<
            std::promise<std::tuple<boost::beast::error_code, std::string, std::string>>>();
        vPromise->push_back(p);

        std::string host = peer.address();
        uint16_t port = peer.port();

        auto self = std::weak_ptr<WsService>(shared_from_this());
        m_connector->connectToWsServer(host, port, m_config->disableSsl(),
            [p, self, connectedEndPoint](boost::beast::error_code _ec,
                const std::string& _extErrorMsg,
                std::shared_ptr<WsStreamDelegate> _wsStreamDelegate,
                std::shared_ptr<std::string> _nodeId) {
                auto service = self.lock();
                if (!service)
                {
                    return;
                }

                auto futResult = std::make_tuple(_ec, _extErrorMsg, connectedEndPoint);
                p->set_value(futResult);

                if (_ec)
                {
                    return;
                }

                auto session = service->newSession(_wsStreamDelegate, *_nodeId.get());
                session->setConnectedEndPoint(connectedEndPoint);
                session->startAsClient();
            });
    }

    return vPromise;
}

void WsService::reconnect()
{
    auto self = std::weak_ptr<WsService>(shared_from_this());
    m_reconnect = std::make_shared<boost::asio::deadline_timer>(
        *(m_timerIoc), boost::posix_time::milliseconds(m_config->reconnectPeriod()));

    m_reconnect->async_wait([self, this](const boost::system::error_code& _error) {
        if (_error == boost::asio::error::operation_aborted)
        {
            return;
        }
        try
        {
            auto service = self.lock();
            if (!service)
            {
                return;
            }

            auto connectPeers = std::make_shared<std::set<NodeIPEndpoint>>();

            // select all disconnected nodes
            ReadGuard l(x_peers);
            for (auto& peer : *m_reconnectedPeers)
            {
                std::string connectedEndPoint = peer.address() + ":" + std::to_string(peer.port());
                auto session = getSession(connectedEndPoint);
                if (session)
                {
                    continue;
                }
                connectPeers->insert(peer);
            }


            if (!connectPeers->empty())
            {
                for (auto reconnectPeer : *connectPeers)
                {
                    WEBSOCKET_SERVICE(INFO)
                        << ("reconnect")
                        << LOG_KV("peer", reconnectPeer.address() + ":" +
                                              std::to_string(reconnectPeer.port()));
                }
                asyncConnectToEndpoints(connectPeers);
            }


            service->reconnect();
        }
        catch (std::exception const& e)
        {
            BOOST_SSL_LOG(WARNING) << LOG_DESC("reconnect exception")
                                   << LOG_KV("error", boost::diagnostic_information(e));
        }
    });
}

bool WsService::registerMsgHandler(uint16_t _msgType, MsgHandler _msgHandler)
{
    UpgradableGuard l(x_msgTypeHandlers);
    if (m_msgType2Method.count(_msgType) || !_msgHandler)
    {
        return false;
    }
    UpgradeGuard ul(l);
    m_msgType2Method[_msgType] = _msgHandler;
    return true;
}

MsgHandler WsService::getMsgHandler(uint16_t _type)
{
    ReadGuard l(x_msgTypeHandlers);
    if (m_msgType2Method.count(_type))
    {
        return m_msgType2Method[_type];
    }
    return nullptr;
}

bool WsService::eraseMsgHandler(uint16_t _type)
{
    UpgradableGuard l(x_msgTypeHandlers);
    if (!m_msgType2Method.count(_type))
    {
        return false;
    }
    UpgradeGuard ul(l);
    m_msgType2Method.erase(_type);
    return true;
}

std::shared_ptr<WsSession> WsService::newSession(
    std::shared_ptr<WsStreamDelegate> _wsStreamDelegate, std::string const& _nodeId)
{
    _wsStreamDelegate->setMaxReadMsgSize(m_config->maxMsgSize());

    std::string endPoint = _wsStreamDelegate->remoteEndpoint();
    auto session = m_sessionFactory->createSession(m_moduleName);

    session->setWsStreamDelegate(_wsStreamDelegate);
    session->setIoc(m_ioservicePool->getIOService());
    session->setThreadPool(threadPool());
    session->setMessageFactory(messageFactory());
    session->setEndPoint(endPoint);
    session->setConnectedEndPoint(endPoint);
    session->setMaxWriteMsgSize(m_config->maxMsgSize());
    session->setSendMsgTimeout(m_config->sendMsgTimeout());
    session->setNodeId(_nodeId);

    auto self = std::weak_ptr<WsService>(shared_from_this());
    session->setConnectHandler([self](Error::Ptr _error, std::shared_ptr<WsSession> _session) {
        auto wsService = self.lock();
        if (wsService)
        {
            wsService->onConnect(_error, _session);
        }
    });
    session->setDisconnectHandler(
        [self](Error::Ptr _error, std::shared_ptr<ws::WsSession> _session) {
            auto wsService = self.lock();
            if (wsService)
            {
                wsService->onDisconnect(_error, _session);
            }
        });
    session->setRecvMessageHandler(
        [self](std::shared_ptr<boostssl::MessageFace> _msg, std::shared_ptr<WsSession> _session) {
            auto wsService = self.lock();
            if (wsService)
            {
                wsService->onRecvMessage(_msg, _session);
            }
        });

    WEBSOCKET_SERVICE(INFO) << LOG_BADGE("newSession") << LOG_DESC("start the session")
                            << LOG_KV("endPoint", endPoint);
    return session;
}

void WsService::addSession(std::shared_ptr<WsSession> _session)
{
    auto connectedEndPoint = _session->connectedEndPoint();
    auto endpoint = _session->endPoint();
    bool ok = false;
    {
        boost::unique_lock<boost::shared_mutex> lock(x_mutex);
        auto it = m_sessions.find(connectedEndPoint);
        if (it == m_sessions.end())
        {
            m_sessions[connectedEndPoint] = _session;
            ok = true;
        }
    }

    // thread pool
    for (auto& conHandler : m_connectHandlers)
    {
        conHandler(_session);
    }

    WEBSOCKET_SERVICE(INFO) << LOG_BADGE("addSession") << LOG_DESC("add session to mapping")
                            << LOG_KV("connectedEndPoint", connectedEndPoint)
                            << LOG_KV("endPoint", endpoint) << LOG_KV("result", ok);
}

void WsService::removeSession(const std::string& _endPoint)
{
    {
        boost::unique_lock<boost::shared_mutex> lock(x_mutex);
        m_sessions.erase(_endPoint);
    }

    WEBSOCKET_SERVICE(INFO) << LOG_BADGE("removeSession") << LOG_KV("endpoint", _endPoint);
}

std::shared_ptr<WsSession> WsService::getSession(const std::string& _endPoint)
{
    boost::shared_lock<boost::shared_mutex> lock(x_mutex);
    auto it = m_sessions.find(_endPoint);
    if (it != m_sessions.end())
    {
        return it->second;
    }
    return nullptr;
}

WsSessions WsService::sessions()
{
    WsSessions sessions;
    {
        boost::shared_lock<boost::shared_mutex> lock(x_mutex);
        for (const auto& session : m_sessions)
        {
            if (session.second && session.second->isConnected())
            {
                sessions.push_back(session.second);
            }
        }
    }

    return sessions;
}

/**
 * @brief: session connect
 * @param _error:
 * @param _session: session
 * @return void:
 */
void WsService::onConnect(Error::Ptr _error, std::shared_ptr<WsSession> _session)
{
    std::ignore = _error;
    std::string endpoint = "";
    std::string connectedEndPoint = "";
    if (_session)
    {
        endpoint = _session->endPoint();
        connectedEndPoint = _session->connectedEndPoint();
    }

    addSession(_session);

    WEBSOCKET_SERVICE(INFO) << LOG_BADGE("onConnect") << LOG_KV("endpoint", endpoint)
                            << LOG_KV("connectedEndPoint", connectedEndPoint)
                            << LOG_KV("refCount", _session.use_count());
}

/**
 * @brief: session disconnect
 * @param _error: the reason of disconnection
 * @param _session: session
 * @return void:
 */
void WsService::onDisconnect(Error::Ptr _error, std::shared_ptr<WsSession> _session)
{
    std::ignore = _error;
    std::string endpoint = "";
    std::string connectedEndPoint = "";
    if (_session)
    {
        endpoint = _session->endPoint();
        connectedEndPoint = _session->connectedEndPoint();
    }

    // clear the session
    removeSession(connectedEndPoint);

    for (auto& disHandler : m_disconnectHandlers)
    {
        disHandler(_session);
    }

    WEBSOCKET_SERVICE(INFO) << LOG_BADGE("onDisconnect") << LOG_KV("endpoint", endpoint)
                            << LOG_KV("connectedEndPoint", connectedEndPoint)
                            << LOG_KV("refCount", _session ? _session.use_count() : -1);
}

void WsService::onRecvMessage(
    std::shared_ptr<boostssl::MessageFace> _msg, std::shared_ptr<WsSession> _session)
{
    auto seq = _msg->seq();

    WEBSOCKET_SERVICE(TRACE) << LOG_BADGE("onRecvMessage")
                             << LOG_DESC("receive message from server")
                             << LOG_KV("type", _msg->packetType()) << LOG_KV("seq", seq)
                             << LOG_KV("endpoint", _session->endPoint())
                             << LOG_KV("data size", _msg->payload()->size())
                             << LOG_KV("use_count", _session.use_count());

    auto typeHandler = getMsgHandler(_msg->packetType());
    if (typeHandler)
    {
        typeHandler(_msg, _session);
    }
    else
    {
        WEBSOCKET_SERVICE(WARNING)
            << LOG_BADGE("onRecvMessage") << LOG_DESC("unrecognized message type")
            << LOG_KV("type", _msg->packetType()) << LOG_KV("endpoint", _session->endPoint())
            << LOG_KV("seq", seq) << LOG_KV("data size", _msg->payload()->size())
            << LOG_KV("use_count", _session.use_count());
    }
}

void WsService::asyncSendMessageByEndPoint(const std::string& _endPoint,
    std::shared_ptr<boostssl::MessageFace> _msg, Options _options, RespCallBack _respFunc)
{
    std::shared_ptr<WsSession> session = getSession(_endPoint);
    if (!session)
    {
        if (_respFunc)
        {
            auto error = std::make_shared<Error>(
                WsError::EndPointNotExist, "there has no connection of the endpoint exist");
            _respFunc(error, nullptr, nullptr);
        }

        return;
    }

    session->asyncSendMessage(_msg, _options, _respFunc);
}

void WsService::asyncSendMessage(
    std::shared_ptr<boostssl::MessageFace> _msg, Options _options, RespCallBack _respCallBack)
{
    auto seq = _msg->seq();
    return asyncSendMessage(sessions(), _msg, _options, _respCallBack);
}

void WsService::asyncSendMessage(const WsSessions& _ss, std::shared_ptr<boostssl::MessageFace> _msg,
    Options _options, RespCallBack _respFunc)
{
    class Retry : public std::enable_shared_from_this<Retry>
    {
    public:
        WsSessions ss;
        std::shared_ptr<boostssl::MessageFace> msg;
        Options options;
        RespCallBack respFunc;

    public:
        void trySendMessageWithOutCB()
        {
            if (ss.empty())
            {
                return;
            }

            auto seed = std::chrono::system_clock::now().time_since_epoch().count();
            std::default_random_engine e(seed);
            std::shuffle(ss.begin(), ss.end(), e);

            auto session = *ss.begin();
            ss.erase(ss.begin());

 
            session->asyncSendMessage(msg, options);
        }

        void trySendMessageWithCB()
        {
            if (ss.empty())
            {
                auto error = std::make_shared<Error>(
                    WsError::NoActiveCons, "there has no active connection available");
                respFunc(error, nullptr, nullptr);
                return;
            }

            auto seed = std::chrono::system_clock::now().time_since_epoch().count();
            std::default_random_engine e(seed);
            std::shuffle(ss.begin(), ss.end(), e);

            auto session = *ss.begin();
            ss.erase(ss.begin());

            auto self = shared_from_this();
            std::string endPoint = session->endPoint();
            auto moduleName = session->moduleName();
            // Note: should not pass session to the lamda operator[], this will lead to memory leak
            session->asyncSendMessage(msg, options,
                [self, endPoint, moduleName, callback = respFunc](Error::Ptr _error,
                    std::shared_ptr<boostssl::MessageFace> _msg,
                    std::shared_ptr<WsSession> _session) {
                    if (_error && _error->errorCode() != 0)
                    {
                        BOOST_SSL_LOG(WARNING)
                            << LOG_BADGE(moduleName) << LOG_BADGE("asyncSendMessage")
                            << LOG_DESC("callback error") << LOG_KV("endpoint", endPoint)
                            << LOG_KV("errorCode", _error->errorCode())
                            << LOG_KV("errorMessage", _error->errorMessage());

                        if (notRetryAgain(_error->errorCode()))
                        {
                            return self->respFunc(_error, _msg, _session);
                        }

                        // retry
                        return self->trySendMessageWithCB();
                    }

                    callback(_error, _msg, _session);
                });
        }
    };

    auto retry = std::make_shared<Retry>();
    retry->ss = _ss;
    retry->msg = _msg;

    retry->options = _options;
    retry->respFunc = _respFunc;

    if (_respFunc)
    {
        retry->trySendMessageWithCB();
    }
    else
    {
        retry->trySendMessageWithOutCB();
    }

    // auto size = _ss.size();
    // auto seq = _msg->seq();
    // int32_t timeout = _options.timeout > 0 ? _options.timeout : m_config->sendMsgTimeout();

    // WEBSOCKET_SERVICE(DEBUG) << LOG_BADGE("asyncSendMessage") <<
    // LOG_KV("seq", seq)
    //                          << LOG_KV("size", size) << LOG_KV("timeout", timeout);
}

void WsService::asyncSendMessage(const std::set<std::string>& _endPoints,
    std::shared_ptr<boostssl::MessageFace> _msg, Options _options, RespCallBack _respFunc)
{
    ws::WsSessions ss;
    for (const std::string& endPoint : _endPoints)
    {
        auto s = getSession(endPoint);
        if (s)
        {
            ss.push_back(s);
        }
        else
        {
            WEBSOCKET_SERVICE(DEBUG)
                << LOG_BADGE("asyncSendMessage")
                << LOG_DESC("there has no connection of the endpoint exist, skip")
                << LOG_KV("endPoint", endPoint);
        }
    }

    return asyncSendMessage(ss, _msg, _options, _respFunc);
}

void WsService::broadcastMessage(std::shared_ptr<boostssl::MessageFace> _msg)
{
    broadcastMessage(sessions(), _msg);
}

void WsService::broadcastMessage(
    const WsSession::Ptrs& _ss, std::shared_ptr<boostssl::MessageFace> _msg)
{
    for (auto& session : _ss)
    {
        if (session->isConnected())
        {
            session->asyncSendMessage(_msg);
        }
    }

    WEBSOCKET_SERVICE(DEBUG) << LOG_BADGE("broadcastMessage");
}
