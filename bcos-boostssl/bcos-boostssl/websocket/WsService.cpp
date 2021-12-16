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
#include <bcos-boostssl/utilities/BoostLog.h>
#include <bcos-boostssl/utilities/Common.h>
#include <bcos-boostssl/utilities/ThreadPool.h>
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsError.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <json/json.h>
#include <boost/core/ignore_unused.hpp>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>


using namespace bcos;
using namespace bcos::boostssl;
using namespace bcos::boostssl::utilities;
using namespace bcos::boostssl::ws;

WsService::WsService()
{
    WEBSOCKET_SERVICE(INFO) << LOG_KV("[NEWOBJ][WsService]", this);
}

WsService::~WsService()
{
    stop();
    WEBSOCKET_SERVICE(INFO) << LOG_KV("[DELOBJ][WsService]", this);
}

void WsService::waitForConnectionEstablish()
{
    auto start = std::chrono::high_resolution_clock::now();
    auto end = start + std::chrono::milliseconds(m_waitConnectFinishTimeout);

    while (true)
    {
        // sleep for connection establish
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto ss = sessions();
        if (!ss.empty())
        {
            break;
        }

        if (std::chrono::high_resolution_clock::now() < end)
        {
            continue;
        }
        else
        {
            stop();
            WEBSOCKET_SERVICE(WARNING) << LOG_BADGE("waitForConnectionEstablish")
                                       << LOG_DESC("the connection to the server timed out")
                                       << LOG_KV("timeout", m_waitConnectFinishTimeout);

            BOOST_THROW_EXCEPTION(std::runtime_error("The connection to the server timed out"));
            return;
        }
    }
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
    startIocThread();

    // start as server
    if (m_config->asServer())
    {
        m_httpServer->start();
    }

    // start as client
    if (m_config->asClient())
    {
        if (m_config->connectedPeers())
        {
            reconnect();
        }

        // Note: block until at least one connection establish
        if (!m_config->connectedPeers()->empty() && m_waitConnectFinish)
        {
            waitForConnectionEstablish();
        }
    }

    // heartbeat
    heartbeat();

    WEBSOCKET_SERVICE(INFO) << LOG_BADGE("start")
                            << LOG_DESC("start websocket service successfully")
                            << LOG_KV("model", m_config->model());
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
    if (m_ioc && !m_ioc->stopped())
    {
        m_ioc->stop();
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
    // stop m_iocThread
    if (m_iocThread->get_id() != std::this_thread::get_id())
    {
        m_iocThread->join();
        m_iocThread.reset();
    }
    else
    {
        m_iocThread->detach();
    }

    WEBSOCKET_SERVICE(INFO) << LOG_BADGE("stop") << LOG_DESC("stop websocket service successfully");
}

void WsService::startIocThread()
{
    m_iocThread = std::make_shared<std::thread>([&] {
        while (m_running)
        {
            try
            {
                m_ioc->run();
            }
            catch (const std::exception& e)
            {
                WEBSOCKET_SERVICE(WARNING)
                    << LOG_BADGE("startIocThread") << LOG_DESC("Exception in IOC Thread:")
                    << boost::diagnostic_information(e);
            }

            m_ioc->reset();
        }

        WEBSOCKET_SERVICE(INFO) << LOG_BADGE("startIocThread") << "IOC thread exit";
    });
}

void WsService::heartbeat()
{
    auto ss = sessions();

    /*
    for (auto const& s : ss)
    {
        s->ping();
    }
    */

    WEBSOCKET_SERVICE(INFO) << LOG_BADGE("heartbeat") << LOG_DESC("connected nodes")
                            << LOG_KV("count", ss.size());

    m_heartbeat = std::make_shared<boost::asio::deadline_timer>(boost::asio::make_strand(*m_ioc),
        boost::posix_time::milliseconds(m_config->heartbeatPeriod()));
    auto self = std::weak_ptr<WsService>(shared_from_this());
    m_heartbeat->async_wait([self](const boost::system::error_code&) {
        auto service = self.lock();
        if (!service)
        {
            return;
        }
        service->heartbeat();
    });
}

void WsService::asyncConnectOnce(EndPointsConstPtr _peers)
{
    for (auto const& peer : *_peers)
    {
        std::string connectedEndPoint = peer.host + ":" + std::to_string(peer.port);

        WEBSOCKET_SERVICE(DEBUG) << LOG_BADGE("connectOnce") << LOG_DESC("try to connect to peer")
                                 << LOG_KV("endpoint", connectedEndPoint);

        std::string host = peer.host;
        uint16_t port = peer.port;

        auto self = std::weak_ptr<WsService>(shared_from_this());
        m_connector->connectToWsServer(host, port,
            [self, connectedEndPoint](
                boost::beast::error_code _ec, std::shared_ptr<WsStream> _stream) {
                auto service = self.lock();
                if (!service)
                {
                    return;
                }

                if (_ec)
                {
                    return;
                }

                auto session = service->newSession(_stream);
                // reset connected endpoint
                session->setConnectedEndPoint(connectedEndPoint);
                session->startAsClient();
            });
    }
}

void WsService::reconnect()
{
    auto waitConnectedPeers = std::make_shared<std::vector<EndPoint>>();

    // select all disconnected nodes
    auto peers = m_config->connectedPeers();
    for (auto const& peer : *peers)
    {
        std::string connectedEndPoint = peer.host + ":" + std::to_string(peer.port);
        auto session = getSession(connectedEndPoint);
        if (session)
        {
            continue;
        }

        waitConnectedPeers->push_back(peer);
    }

    if (!waitConnectedPeers->empty())
    {
        asyncConnectOnce(waitConnectedPeers);
    }

    m_reconnect = std::make_shared<boost::asio::deadline_timer>(boost::asio::make_strand(*m_ioc),
        boost::posix_time::milliseconds(m_config->reconnectPeriod()));
    auto self = std::weak_ptr<WsService>(shared_from_this());
    m_reconnect->async_wait([self](const boost::system::error_code&) {
        auto service = self.lock();
        if (!service)
        {
            return;
        }
        service->reconnect();
    });
}

bool WsService::registerMsgHandler(uint32_t _msgType, MsgHandler _msgHandler)
{
    auto it = m_msgType2Method.find(_msgType);
    if (it == m_msgType2Method.end())
    {
        m_msgType2Method[_msgType] = _msgHandler;
        return true;
    }
    return false;
}

std::shared_ptr<WsSession> WsService::newSession(std::shared_ptr<WsStream> _stream)
{
    auto wsSession = std::make_shared<WsSession>();
    std::string endPoint = _stream->remoteEndpoint();
    wsSession->setStream(_stream);

    wsSession->setIoc(ioc());
    wsSession->setThreadPool(threadPool());
    wsSession->setMessageFactory(messageFactory());
    wsSession->setEndPoint(endPoint);
    wsSession->setConnectedEndPoint(endPoint);
    wsSession->setSendMsgTimeout(m_sendMsgTimeout);

    auto self = std::weak_ptr<WsService>(shared_from_this());
    wsSession->setConnectHandler([self](Error::Ptr _error, std::shared_ptr<WsSession> _session) {
        auto wsService = self.lock();
        if (wsService)
        {
            wsService->onConnect(_error, _session);
        }
    });
    wsSession->setDisconnectHandler(
        [self](Error::Ptr _error, std::shared_ptr<ws::WsSession> _session) {
            auto wsService = self.lock();
            if (wsService)
            {
                wsService->onDisconnect(_error, _session);
            }
        });
    wsSession->setRecvMessageHandler(
        [self](std::shared_ptr<WsMessage> _msg, std::shared_ptr<WsSession> _session) {
            auto wsService = self.lock();
            if (wsService)
            {
                wsService->onRecvMessage(_msg, _session);
            }
        });

    WEBSOCKET_SERVICE(INFO) << LOG_BADGE("newSession") << LOG_DESC("start the session")
                            << LOG_KV("endPoint", endPoint);
    return wsSession;
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

    // WEBSOCKET_SERVICE(TRACE) << LOG_BADGE("sessions") << LOG_KV("size", sessions.size());
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
    boost::ignore_unused(_error);
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
    boost::ignore_unused(_error);
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

void WsService::onRecvMessage(std::shared_ptr<WsMessage> _msg, std::shared_ptr<WsSession> _session)
{
    auto seq = std::string(_msg->seq()->begin(), _msg->seq()->end());

    WEBSOCKET_SERVICE(DEBUG) << LOG_BADGE("onRecvMessage")
                             << LOG_DESC("receive message from server")
                             << LOG_KV("type", _msg->type()) << LOG_KV("seq", seq)
                             << LOG_KV("endpoint", _session->endPoint())
                             << LOG_KV("data size", _msg->data()->size())
                             << LOG_KV("use_count", _session.use_count());

    auto it = m_msgType2Method.find(_msg->type());
    if (it != m_msgType2Method.end())
    {
        auto callback = it->second;
        callback(_msg, _session);
    }
    else
    {
        WEBSOCKET_SERVICE(WARNING)
            << LOG_BADGE("onRecvMessage") << LOG_DESC("unrecognized message type")
            << LOG_KV("type", _msg->type()) << LOG_KV("endpoint", _session->endPoint())
            << LOG_KV("seq", seq) << LOG_KV("data size", _msg->data()->size())
            << LOG_KV("use_count", _session.use_count());
    }
}

void WsService::asyncSendMessageByEndPoint(const std::string& _endPoint,
    std::shared_ptr<WsMessage> _msg, Options _options, RespCallBack _respFunc)
{
    std::shared_ptr<WsSession> session = getSession(_endPoint);
    if (!session)
    {
        auto error = std::make_shared<Error>(
            WsError::EndPointNotExist, "there has no connection of the endpoint exist");
        _respFunc(error, nullptr, nullptr);
        return;
    }

    session->asyncSendMessage(_msg, _options, _respFunc);
}

void WsService::asyncSendMessage(
    std::shared_ptr<WsMessage> _msg, Options _options, RespCallBack _respCallBack)
{
    auto seq = std::string(_msg->seq()->begin(), _msg->seq()->end());
    return asyncSendMessage(sessions(), _msg, _options, _respCallBack);
}

void WsService::asyncSendMessage(const WsSessions& _ss, std::shared_ptr<WsMessage> _msg,
    Options _options, RespCallBack _respFunc)
{
    class Retry : public std::enable_shared_from_this<Retry>
    {
    public:
        WsSessions ss;
        std::shared_ptr<WsMessage> msg;
        Options options;
        RespCallBack respFunc;

    public:
        void sendMessage()
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
            session->asyncSendMessage(msg, options,
                [self, session](Error::Ptr _error, std::shared_ptr<WsMessage> _msg,
                    std::shared_ptr<WsSession> _session) {
                    if (_error && _error->errorCode() != protocol::CommonError::SUCCESS)
                    {
                        WEBSOCKET_SERVICE(WARNING)
                            << LOG_BADGE("asyncSendMessage") << LOG_DESC("callback error")
                            << LOG_KV("endpoint", session->endPoint())
                            << LOG_KV("errorCode", _error->errorCode())
                            << LOG_KV("errorMessage", _error->errorMessage());
                        return self->sendMessage();
                    }

                    self->respFunc(_error, _msg, _session);
                });
        }
    };

    auto size = _ss.size();

    auto retry = std::make_shared<Retry>();
    retry->ss = _ss;
    retry->msg = _msg;
    retry->options = _options;
    retry->respFunc = _respFunc;
    retry->sendMessage();

    auto seq = std::string(_msg->seq()->begin(), _msg->seq()->end());
    int32_t timeout = _options.timeout > 0 ? _options.timeout : m_sendMsgTimeout;

    WEBSOCKET_SERVICE(DEBUG) << LOG_BADGE("asyncSendMessage") << LOG_KV("seq", seq)
                             << LOG_KV("size", size) << LOG_KV("timeout", timeout);
}

void WsService::asyncSendMessage(const std::set<std::string>& _endPoints,
    std::shared_ptr<WsMessage> _msg, Options _options, RespCallBack _respFunc)
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


void WsService::broadcastMessage(std::shared_ptr<WsMessage> _msg)
{
    broadcastMessage(sessions(), _msg);
}

void WsService::broadcastMessage(const WsSession::Ptrs& _ss, std::shared_ptr<WsMessage> _msg)
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