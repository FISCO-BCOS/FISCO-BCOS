/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @file: ChannelRPCServer.cpp
 * @author: monan
 * @date: 2017
 *
 * @author xingqiangbai
 * @date 2018.11.5
 * @modify use libp2p to send message
 */

#include "ChannelRPCServer.h"
#include "libchannelserver/ChannelException.h"  // for CHANNEL_LOG
#include "libchannelserver/ChannelMessage.h"    // for TopicChannelM...
#include "libchannelserver/ChannelServer.h"     // for ChannelServer
#include "libchannelserver/ChannelSession.h"    // for ChannelSessio...
#include "libnetwork/Common.h"                  // for NetworkException
#include "libp2p/P2PInterface.h"                // for P2PInterface
#include "libp2p/P2PMessageFactory.h"           // for P2PMessageFac...
#include "libp2p/P2PSession.h"                  // for P2PSession
#include "libprotocol/Protocol.h"               // for AMOP, ProtocolID
#include "libutilities/Common.h"                // for bytes, byte
#include <json/json.h>
#include <libeventfilter/Common.h>
#include <libp2p/P2PMessage.h>
#include <libp2p/Service.h>
#include <librpc/StatisticProtocolServer.h>
#include <unistd.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/random.hpp>
#include <boost/range/algorithm/remove_if.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <string>

using namespace std;
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::channel;
using namespace bcos::p2p;

static const int c_seqAbridgedLen = 8;

ChannelRPCServer::~ChannelRPCServer()
{
    StopListening();
}

bool ChannelRPCServer::StartListening()
{
    if (!_running)
    {
        CHANNEL_LOG(TRACE) << "Start ChannelRPCServer" << LOG_KV("Host", _listenAddr) << ":"
                           << _listenPort;

        std::function<void(bcos::channel::ChannelException, bcos::channel::ChannelSession::Ptr)>
            fp = std::bind(&ChannelRPCServer::onConnect, shared_from_this(), std::placeholders::_1,
                std::placeholders::_2);
        _server->setConnectionHandler(fp);

        _server->run();

        std::function<void(bcos::network::NetworkException, std::shared_ptr<bcos::p2p::P2PSession>,
            p2p::P2PMessage::Ptr)>
            channelHandler = std::bind(&ChannelRPCServer::onNodeChannelRequest, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

        m_service->registerHandlerByProtoclID(bcos::protocol::ProtocolID::AMOP, channelHandler);

        CHANNEL_LOG(INFO) << "ChannelRPCServer started";

        _running = true;
    }

    return true;
}

bool ChannelRPCServer::StopListening()
{
    if (_running)
    {
        _server->stop();
    }

    _running = false;

    return true;
}

bool ChannelRPCServer::SendResponse(const std::string& _response, void* _addInfo)
{
    std::string addInfo = *((std::string*)_addInfo);

    std::string seq;
    ChannelSession::Ptr session;
    {
        std::lock_guard<std::mutex> lock(_seqMutex);
        auto it = _seq2session.find(addInfo);

        if (it != _seq2session.end())
        {
            seq = it->first;
            session = it->second;
            _seq2session.erase(it);
        }

        delete (std::string*)_addInfo;
    }

    if (session)
    {
        CHANNEL_LOG(TRACE) << "send ethereum resp seq"
                           << LOG_KV("seq", seq.substr(0, c_seqAbridgedLen))
                           << LOG_KV("response", _response);

        std::shared_ptr<bytes> resp(new bytes());

        auto message = session->messageFactory()->buildMessage();
        message->setSeq(seq);
        message->setResult(0);
        message->setType(0x12);
        message->setData((const byte*)_response.data(), _response.size());

        session->asyncSendMessage(message, bcos::channel::ChannelSession::CallbackType(), 0);
    }
    else
    {
        CHANNEL_LOG(ERROR) << "not found seq may be timeout";
    }

    return false;
}

void bcos::ChannelRPCServer::removeSession(int sessionID)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto it = _sessions.find(sessionID);

    if (it != _sessions.end())
    {
        _sessions.erase(it);
    }
}

void ChannelRPCServer::onConnect(
    bcos::channel::ChannelException e, bcos::channel::ChannelSession::Ptr session)
{
    if (e.errorCode() == 0)
    {
        CHANNEL_LOG(INFO) << "channel new connect, host=" << session->host() << ":"
                          << session->port();

        session->setNetworkStat(m_networkStatHandler);

        auto sessionID = ++_sessionCount;
        {
            std::lock_guard<std::mutex> lock(_sessionMutex);
            _sessions.insert(std::make_pair(sessionID, session));
        }

        std::function<void(bcos::channel::ChannelSession::Ptr, bcos::channel::ChannelException,
            bcos::channel::Message::Ptr)>
            fp = std::bind(&bcos::ChannelRPCServer::onClientRequest, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3);
        session->setMessageHandler(fp);

        session->run();
        CHANNEL_LOG(INFO) << "start receive";
    }
    else
    {
        CHANNEL_LOG(ERROR) << "onConnect error" << LOG_KV("errorCode", e.errorCode())
                           << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelRPCServer::onDisconnect(
    bcos::channel::ChannelException, bcos::channel::ChannelSession::Ptr session)
{
    CHANNEL_LOG(DEBUG) << "onDisconnect remove session" << LOG_KV("From", session->host()) << ":"
                       << session->port();

    {
        std::lock_guard<std::mutex> lockSession(_sessionMutex);
        std::lock_guard<std::mutex> lockSeqMutex(_seqMutex);

        for (auto& it : _sessions)
        {
            if (it.second == session)
            {
                _sessions.erase(it.first);
                CHANNEL_LOG(DEBUG) << "sessions removed";
                break;
            }
        }

        for (auto& it : _seq2session)
        {
            if (it.second == session)
            {
                _seq2session.erase(it.first);
                CHANNEL_LOG(DEBUG) << "seq2session removed";
                break;
            }
        }
    }

    updateHostTopics();
}

void bcos::ChannelRPCServer::blockNotify(int16_t _groupID, int64_t _blockNumber)
{
    std::string topic = "_block_notify_" + std::to_string(_groupID);
    std::vector<bcos::channel::ChannelSession::Ptr> activedSessions = getSessionByTopic(topic);
    if (activedSessions.empty())
    {
        CHANNEL_LOG(TRACE) << "no session use topic" << LOG_KV("topic", topic);
        return;
    }
    std::shared_ptr<bcos::channel::TopicChannelMessage> message =
        std::make_shared<bcos::channel::TopicChannelMessage>();
    message->setType(BLOCK_NOTIFY);
    message->setSeq(std::string(32, '0'));
    message->setResult(0);
    std::string content =
        std::to_string(_groupID) + "," + boost::lexical_cast<std::string>(_blockNumber);
    Json::Value response;
    response["groupID"] = _groupID;
    response["blockNumber"] = _blockNumber;
    Json::FastWriter writer;
    auto resp = writer.write(response);
    for (auto session : activedSessions)
    {
        message->clearData();
        // default is v1
        if (session->protocolVersion() == bcos::ProtocolVersion::v1)
        {
            message->setTopicData(topic, (const byte*)content.data(), content.size());
        }
        else
        {
            message->setTopicData(topic, (const byte*)resp.data(), resp.size());
        }
        session->asyncSendMessage(
            message, std::function<void(bcos::channel::ChannelException, Message::Ptr)>(), 0);

        CHANNEL_LOG(INFO) << "Push channel message success" << LOG_KV("topic", topic)
                          << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen))
                          << LOG_KV("session", session->host()) << ":" << session->port();
    }
}

void bcos::ChannelRPCServer::onClientRequest(bcos::channel::ChannelSession::Ptr session,
    bcos::channel::ChannelException e, bcos::channel::Message::Ptr message)
{
    if (e.errorCode() == 0)
    {
        CHANNEL_LOG(TRACE) << "receive sdk message" << LOG_KV("length", message->length())
                           << LOG_KV("type", message->type())
                           << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen));

        switch (message->type())
        {
        case CHANNEL_RPC_REQUEST:
            onClientRPCRequest(session, message);
            break;
        case CLIENT_HEARTBEAT:
            onClientHeartbeat(session, message);
            break;
        case CLIENT_HANDSHAKE:
            onClientHandshake(session, message);
            break;
        case CLIENT_REGISTER_EVENT_LOG:
            onClientRegisterEventLogRequest(session, message);
            break;
        case CLIENT_UNREGISTER_EVENT_LOG:
            onClientUnregisterEventLogRequest(session, message);
            break;
        case AMOP_REQUEST:
        case AMOP_RESPONSE:
        case AMOP_MULBROADCAST:
            onClientChannelRequest(session, message);
            break;
        case AMOP_CLIENT_SUBSCRIBE_TOPICS:
            onClientTopicRequest(session, message);
            break;
        case UPDATE_TOPIICSTATUS:
            onClientUpdateTopicStatusRequest(message);
            break;
        default:
            CHANNEL_LOG(ERROR) << "unknown client message" << LOG_KV("type", message->type());
            break;
        }
    }
    else
    {
        CHANNEL_LOG(WARNING) << "onClientRequest" << LOG_KV("errorCode", e.errorCode())
                             << LOG_KV("what", e.what());

        onDisconnect(bcos::channel::ChannelException(), session);
    }
}


void bcos::ChannelRPCServer::onClientUpdateTopicStatusRequest(
    bcos::channel::Message::Ptr channelMessage)
{
    try
    {
        std::string content(
            channelMessage->data(), channelMessage->data() + channelMessage->dataSize());
        Json::Value root;
        Json::Reader jsonReader;
        if (!jsonReader.parse(content, root))
        {
            CHANNEL_LOG(ERROR) << "parse to json failed" << LOG_KV("content:", content);
            return;
        }

        if (!root.isMember("checkResult") || !root["checkResult"].isInt())
        {
            CHANNEL_LOG(ERROR) << "json value have no check_result or check_result is not int";
            return;
        }
        int32_t checkResult = root["checkResult"].asInt();
        if (!root.isMember("nodeId") || !root["nodeId"].isString())
        {
            CHANNEL_LOG(ERROR) << "json value have no nodeId or nodeId is not string";
            return;
        }
        NodeID nodeid(root["nodeId"].asString());

        if (!root.isMember("topic") || !root["topic"].isString())
        {
            CHANNEL_LOG(ERROR) << "json value have no topic or topic is not string";
            return;
        }
        std::string topic = root["topic"].asString();

        CHANNEL_LOG(TRACE) << "Receive sdk request 0x38(update topic status):"
                           << LOG_KV("topic", topic) << LOG_KV("nodeId", root["nodeId"].asString())
                           << LOG_KV("checkResult", checkResult);
        bcos::p2p::P2PSession::Ptr session = m_service->getP2PSessionByNodeId(nodeid);
        if (session)
        {
            session->updateTopicStatus(
                topic, checkResult == 0 ? bcos::VERIFY_SUCCESS_STATUS : bcos::VERIFY_FAILED_STATUS);
        }
    }
    catch (ChannelException& e)
    {
        CHANNEL_LOG(ERROR) << "update topic status failed: " << boost::diagnostic_information(e);
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "update topic status failed: " << boost::diagnostic_information(e);
    }
}

void bcos::ChannelRPCServer::onClientRPCRequest(
    bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message)
{
    // CHANNEL_LOG(DEBUG) << "receive client ethereum request";

    std::string body(message->data(), message->data() + message->dataSize());

    CHANNEL_LOG(TRACE) << "client ethereum request"
                       << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen))
                       << LOG_KV("ethereum request",
                              std::string((char*)message->data(), message->dataSize()));

    {
        std::lock_guard<std::mutex> lock(_seqMutex);
        _seq2session.insert(std::make_pair(message->seq(), session));

        if (m_callbackSetter)
        {
            auto seq = message->seq();
            auto sessionRef = std::weak_ptr<bcos::channel::ChannelSession>(session);
            auto serverRef = std::weak_ptr<bcos::channel::ChannelServer>(_server);
            auto protocolVersion = static_cast<uint32_t>(session->protocolVersion());

            m_callbackSetter(
                new std::function<void(const std::string& receiptContext,
                    GROUP_ID _groupId)>([serverRef, sessionRef, seq](
                                            const std::string& receiptContext, GROUP_ID _groupId) {
                    auto server = serverRef.lock();
                    auto session = sessionRef.lock();
                    if (server && session)
                    {
                        auto channelMessage = server->messageFactory()->buildMessage();
                        channelMessage->setType(TRANSACTION_NOTIFY);
                        channelMessage->setGroupID(_groupId);
                        channelMessage->setSeq(seq);
                        channelMessage->setResult(0);
                        channelMessage->setData(
                            (const byte*)receiptContext.c_str(), receiptContext.size());

                        CHANNEL_LOG(TRACE) << "Push transaction notify: " << seq;
                        session->asyncSendMessage(channelMessage,
                            std::function<void(bcos::channel::ChannelException, Message::Ptr)>(),
                            0);
                    }
                }),
                new std::function<uint32_t()>([protocolVersion]() { return protocolVersion; }));
        }
    }

    std::string* addInfo = new std::string(message->seq());

    try
    {
        OnRpcRequest(session, body, addInfo);
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "Error while OnRpcRequest rpc: " << boost::diagnostic_information(e);
    }

    if (m_callbackSetter)
    {
        m_callbackSetter(NULL, NULL);
    }
}

bool bcos::ChannelRPCServer::OnRpcRequest(
    bcos::channel::ChannelSession::Ptr _session, const std::string& request, void* addInfo)
{
    string response;
    auto handler = this->GetHandler();
    if (!handler)
    {
        return false;
    }
    auto statisticProtocolServer = dynamic_cast<StatisticProtocolServer*>(handler);
    statisticProtocolServer->HandleChannelRequest(
        request, response, [this, _session](bcos::GROUP_ID _groupId) {
            return checkSDKPermission(_groupId, _session->remotePublicKey());
        });
    SendResponse(response, addInfo);
    return true;
}

void bcos::ChannelRPCServer::onClientRegisterEventLogRequest(
    bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message)
{
    try
    {
        auto seq = message->seq();
        uint8_t topicLen = *((uint8_t*)message->data());
        // skip topic field
        std::string data(message->data() + topicLen, message->data() + message->dataSize());

        auto sessionRef = std::weak_ptr<bcos::channel::ChannelSession>(session);
        auto serverRef = std::weak_ptr<bcos::channel::ChannelServer>(_server);
        auto protocolVersion = static_cast<uint32_t>(session->protocolVersion());

        auto respCallback = [serverRef, sessionRef](const std::string& _filterID, int32_t _result,
                                const Json::Value& _logs, GROUP_ID const& _groupId) {
            auto server = serverRef.lock();
            auto session = sessionRef.lock();

            if (server && session && session->actived())
            {
                Json::Value jsonResp;
                jsonResp["result"] = _result;
                jsonResp["filterID"] = _filterID;
                jsonResp["logs"] = _logs;

                Json::FastWriter writer;
                auto resp = writer.write(jsonResp);

                auto channelMessage = server->messageFactory()->buildMessage();

                // only used for network statistic
                channelMessage->setGroupID(_groupId);

                channelMessage->setType(EVENT_LOG_PUSH);
                channelMessage->setResult(0);
                channelMessage->setSeq(std::string(32, '0'));
                channelMessage->setData((const byte*)resp.c_str(), resp.size());

                CHANNEL_LOG(TRACE) << LOG_BADGE("EVENT") << LOG_DESC("response callback")
                                   << LOG_KV("filterID", _filterID) << LOG_KV("result", _result)
                                   << LOG_KV("resp", resp);

                session->asyncSendMessage(channelMessage,
                    std::function<void(bcos::channel::ChannelException, Message::Ptr)>(), 0);

                return true;
            }

            return false;
        };

        auto sessionCheckerCallback = [this, serverRef, sessionRef](GROUP_ID _groupId) {
            auto server = serverRef.lock();
            auto session = sessionRef.lock();
            auto sessionActived = server && session && session->actived();
            if (!sessionActived)
            {
                CHANNEL_LOG(DEBUG) << LOG_DESC("Push event failed for session invactived")
                                   << LOG_KV("groupId", _groupId);
                return bcos::event::filter_status::CALLBACK_FAILED;
            }
            if (!checkSDKPermission(_groupId, session->remotePublicKey()))
            {
                return bcos::event::filter_status::REMOTE_PEERS_ACCESS_DENIED;
            }
            return bcos::event::filter_status::CHECK_VALID;
        };

        // checkSDKPermission when receive CLIENT_REGISTER_EVENT_LOG
        int32_t ret = m_eventFilterCallBack(data, protocolVersion, respCallback,
            sessionCheckerCallback, [this, session](bcos::GROUP_ID _groupId) {
                return checkSDKPermission(_groupId, session->remotePublicKey());
            });

        CHANNEL_LOG(TRACE) << "onClientRegisterEventLogRequest" << LOG_KV("seq", seq)
                           << LOG_KV("ret", ret) << LOG_KV("request", data);

        // send event register request back
        Json::Value response;
        response["result"] = ret;
        Json::FastWriter writer;
        auto resp = writer.write(response);

        std::shared_ptr<bcos::channel::TopicChannelMessage> message =
            std::make_shared<bcos::channel::TopicChannelMessage>();
        message->setType(CLIENT_REGISTER_EVENT_LOG);
        message->setSeq(seq);
        message->setResult(0);
        message->setTopicData(std::string(""), (const byte*)resp.data(), resp.size());

        session->asyncSendMessage(message, bcos::channel::ChannelSession::CallbackType(), 0);
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "onClientRegisterEventLogRequest" << boost::diagnostic_information(e);
    }
}

void bcos::ChannelRPCServer::onClientUnregisterEventLogRequest(
    bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message)
{
    try
    {
        auto seq = message->seq();
        uint8_t topicLen = *((uint8_t*)message->data());
        // skip topic field
        std::string data(message->data() + topicLen, message->data() + message->dataSize());
        auto protocolVersion = static_cast<uint32_t>(session->protocolVersion());

        int32_t ret = m_eventCancelFilterCallBack(
            data, protocolVersion, [this, session](bcos::GROUP_ID _groupId) {
                return checkSDKPermission(_groupId, session->remotePublicKey());
            });

        CHANNEL_LOG(TRACE) << "onClientUnregisterEventLogRequest" << LOG_KV("seq", seq)
                           << LOG_KV("ret", ret) << LOG_KV("request", data);

        // send event unregister request back
        Json::Value response;
        response["result"] = ret;
        Json::FastWriter writer;
        auto resp = writer.write(response);

        std::shared_ptr<bcos::channel::TopicChannelMessage> message =
            std::make_shared<bcos::channel::TopicChannelMessage>();
        message->setType(CLIENT_UNREGISTER_EVENT_LOG);
        message->setSeq(seq);
        message->setResult(0);
        message->setTopicData(std::string(""), (const byte*)resp.data(), resp.size());

        session->asyncSendMessage(message, bcos::channel::ChannelSession::CallbackType(), 0);
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "onClientUnregisterEventLogRequest"
                           << boost::diagnostic_information(e);
    }
}

void bcos::ChannelRPCServer::onClientHandshake(
    bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message)
{
    try
    {
        CHANNEL_LOG(DEBUG) << LOG_DESC("on client handshake") << LOG_KV("seq", message->seq());
        std::string data(message->data(), message->data() + message->dataSize());
        Json::Value value;
        Json::Reader jsonReader;
        if (!jsonReader.parse(data, value))
        {
            CHANNEL_LOG(ERROR) << "parse to json failed" << LOG_KV(" data ", data);
            return;
        }

        auto minimumProtocol = static_cast<ProtocolVersion>(value.get("minimumSupport", 1).asInt());
        auto maximumProtocol = static_cast<ProtocolVersion>(value.get("maximumSupport", 1).asInt());
        auto clientType = value.get("clientType", "Unknow Client Type").asString();
        if (session->maximumProtocolVersion() < minimumProtocol ||
            session->minimumProtocolVersion() > maximumProtocol)
        {  // If the scope does not intersect, disconnect
            CHANNEL_LOG(WARNING) << "onClientHandshake failed, unsupported protocol"
                                 << LOG_KV("clientType", clientType)
                                 << LOG_KV("endpoint", session->host())
                                 << LOG_KV("SDKMinSupport", value.get("minimumSupport", 1).asInt())
                                 << LOG_KV("SDKMaxSupport", value.get("maximumSupport", 1).asInt());
            session->disconnectByQuit();
            onDisconnect(bcos::channel::ChannelException(-1, "Unsupport protocol"), session);
            return;
        }
        // Choose the next largest version number
        session->setProtocolVersion(session->maximumProtocolVersion() > maximumProtocol ?
                                        maximumProtocol :
                                        session->maximumProtocolVersion());

        Json::Value response;
        response["protocol"] = static_cast<int>(session->protocolVersion());
        response["nodeVersion"] = g_BCOSConfig.supportedVersion();
        Json::FastWriter writer;
        auto resp = writer.write(response);
        message->setData((const byte*)resp.data(), resp.size());
        session->asyncSendMessage(message, bcos::channel::ChannelSession::CallbackType(), 0);
        CHANNEL_LOG(INFO) << "onClientHandshake" << LOG_KV("ProtocolVersion", response["protocol"])
                          << LOG_KV("clientType", clientType) << LOG_KV("endpoint", session->host())
                          << ":" << session->port();
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "onClientHandshake" << boost::diagnostic_information(e);
    }
}

void bcos::ChannelRPCServer::onClientHeartbeat(
    bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message)
{
    std::string data((char*)message->data(), message->dataSize());
    if (session->protocolVersion() == ProtocolVersion::v1)
    {
        // default is ProtocolVersion::v1
        if (data == "0")
        {
            data = "1";
            message->setData((const byte*)data.data(), data.size());
            session->asyncSendMessage(message, bcos::channel::ChannelSession::CallbackType(), 0);
        }
    }
    // v2 and v3 for now
    else
    {
        Json::Value response;
        response["heartBeat"] = 1;
        Json::FastWriter writer;
        auto resp = writer.write(response);
        message->setData((const byte*)resp.data(), resp.size());
        session->asyncSendMessage(message, bcos::channel::ChannelSession::CallbackType(), 0);
    }
}

void bcos::ChannelRPCServer::asyncPushChannelMessageHandler(
    const std::string& toTopic, const std::string& content)
{
    try
    {
        CHANNEL_LOG(DEBUG) << LOG_DESC("receive node request") << LOG_KV("topic", toTopic)
                           << LOG_KV("content", content);

        if (getSessionByTopic(toTopic).empty())
        {
            CHANNEL_LOG(DEBUG) << LOG_DESC("no SDK follow topic") << LOG_KV("topic", toTopic)
                               << LOG_DESC("just return");
            return;
        }
        bcos::channel::TopicVerifyChannelMessage::Ptr channelMessage =
            std::make_shared<TopicVerifyChannelMessage>();
        channelMessage->setData(content);
        std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
        channelMessage->encode(*buffer);
        CHANNEL_LOG(DEBUG) << LOG_DESC("async push channel message")
                           << LOG_KV("seq", channelMessage->seq())
                           << LOG_KV("type", channelMessage->type())
                           << LOG_KV("datalen", channelMessage->length());

        asyncPushChannelMessage(toTopic, channelMessage,
            [channelMessage](bcos::channel::ChannelException e, bcos::channel::Message::Ptr,
                bcos::channel::ChannelSession::Ptr) {
                if (!e.errorCode())
                {
                    CHANNEL_LOG(DEBUG) << LOG_DESC("push channel message response")
                                       << LOG_KV("seq", channelMessage->seq());
                }
                else
                {
                    CHANNEL_LOG(ERROR)
                        << LOG_DESC("push channel message response")
                        << LOG_KV("seq", channelMessage->seq()) << LOG_KV("errcode", e.errorCode())
                        << LOG_KV("errmsg", e.what());
                }
            });
    }
    catch (ChannelException& e)
    {
        CHANNEL_LOG(ERROR) << LOG_DESC("async push channel message failed:")
                           << boost::diagnostic_information(e);
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << LOG_DESC("async push channel message failed:")
                           << boost::diagnostic_information(e);
    }
}

// Note: No restrictions on AMOP traffic between nodes
void bcos::ChannelRPCServer::onNodeChannelRequest(
    bcos::network::NetworkException, std::shared_ptr<p2p::P2PSession> s, p2p::P2PMessage::Ptr msg)
{
    NodeID nodeID;

    if (s)
    {
        nodeID = s->nodeID();
    }
    else
    {
        nodeID = m_service->id();
    }

    auto channelMessage = _server->messageFactory()->buildMessage();
    ssize_t result = channelMessage->decode(msg->buffer()->data(), msg->buffer()->size());

    if (result <= 0)
    {
        CHANNEL_LOG(ERROR) << "onNodeChannelRequest decode error"
                           << LOG_KV(" package size", msg->buffer()->size());
        return;
    }

    CHANNEL_LOG(DEBUG) << "receive node request" << LOG_KV("from", nodeID)
                       << LOG_KV("length", msg->buffer()->size())
                       << LOG_KV("type", channelMessage->type())
                       << LOG_KV("seq", channelMessage->seq());

    try
    {
        if (channelMessage->dataSize() < 1)
        {
            CHANNEL_LOG(ERROR) << "invalid channel message too short";
            return;
        }

        uint8_t topicLen = *((uint8_t*)channelMessage->data());
        std::string topic((char*)channelMessage->data() + 1, topicLen - 1);

        CHANNEL_LOG(DEBUG) << "onNodeChannelRequest target" << LOG_KV("topic", topic);

        if (channelMessage->type() == AMOP_REQUEST)
        {
            try
            {
                auto p2pMessage = msg;
                auto service = m_service;
                asyncPushChannelMessage(topic, channelMessage,
                    [nodeID, channelMessage, service, p2pMessage](bcos::channel::ChannelException e,
                        bcos::channel::Message::Ptr response, bcos::channel::ChannelSession::Ptr) {
                        if (e.errorCode())
                        {
                            CHANNEL_LOG(ERROR) << "Push channel message failed"
                                               << LOG_KV("what", boost::diagnostic_information(e));

                            channelMessage->setResult(e.errorCode());
                            channelMessage->setType(AMOP_RESPONSE);

                            auto buffer = std::make_shared<bytes>();
                            channelMessage->encode(*buffer);
                            auto p2pResponse = std::dynamic_pointer_cast<bcos::p2p::P2PMessage>(
                                service->p2pMessageFactory()->buildMessage());
                            p2pResponse->setBuffer(buffer);
                            p2pResponse->setProtocolID(-bcos::protocol::ProtocolID::AMOP);
                            p2pResponse->setPacketType(0u);
                            p2pResponse->setSeq(p2pMessage->seq());
                            service->asyncSendMessageByNodeID(nodeID, p2pResponse,
                                CallbackFuncWithSession(), bcos::network::Options());

                            return;
                        }
                        CHANNEL_LOG(TRACE)
                            << "Receive sdk response" << LOG_KV("seq", response->seq());
                        auto buffer = std::make_shared<bytes>();
                        response->encode(*buffer);
                        auto p2pResponse = std::dynamic_pointer_cast<bcos::p2p::P2PMessage>(
                            service->p2pMessageFactory()->buildMessage());
                        p2pResponse->setBuffer(buffer);
                        p2pResponse->setProtocolID(-bcos::protocol::ProtocolID::AMOP);
                        p2pResponse->setPacketType(0u);
                        p2pResponse->setSeq(p2pMessage->seq());
                        service->asyncSendMessageByNodeID(nodeID, p2pResponse,
                            CallbackFuncWithSession(), bcos::network::Options());
                    });
            }
            catch (std::exception& e)
            {
                CHANNEL_LOG(ERROR) << "push message totaly failed"
                                   << LOG_KV("what", boost::diagnostic_information(e));

                channelMessage->setResult(REMOTE_CLIENT_PEER_UNAVAILABLE);
                channelMessage->setType(AMOP_RESPONSE);

                auto buffer = std::make_shared<bytes>();
                channelMessage->encode(*buffer);
                auto p2pResponse = std::dynamic_pointer_cast<bcos::p2p::P2PMessage>(
                    m_service->p2pMessageFactory()->buildMessage());
                p2pResponse->setBuffer(buffer);
                p2pResponse->setProtocolID(bcos::protocol::ProtocolID::AMOP);
                p2pResponse->setPacketType(0u);
                p2pResponse->setSeq(msg->seq());
                m_service->asyncSendMessageByNodeID(
                    nodeID, p2pResponse, CallbackFuncWithSession(), bcos::network::Options());
            }
        }
        else if (channelMessage->type() == AMOP_MULBROADCAST)
        {
            try
            {
                asyncBroadcastChannelMessage(topic, channelMessage);
            }
            catch (std::exception& e)
            {
                CHANNEL_LOG(ERROR)
                    << "Broadcast channel message failed: " << boost::diagnostic_information(e);
            }
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void bcos::ChannelRPCServer::onClientTopicRequest(
    bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message)
{
    CHANNEL_LOG(DEBUG) << LOG_DESC("SDK topic message") << LOG_KV("type", message->type())
                       << LOG_KV("seq", message->seq());

    std::string body(message->data(), message->data() + message->dataSize());

    CHANNEL_LOG(DEBUG) << "SDK topic message"
                       << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen))
                       << LOG_KV("message", body);

    try
    {
        Json::Value root;
        Json::Reader jsonReader;
        if (!jsonReader.parse(body, root))
        {
            CHANNEL_LOG(ERROR) << "parse to json failed" << LOG_KV("content:", body);
            return;
        }
        std::shared_ptr<std::set<std::string> > topics = std::make_shared<std::set<std::string> >();
        Json::Value topicsValue = root;
        if (!topicsValue.empty())
        {
            for (size_t i = 0; i < topicsValue.size(); ++i)
            {
                std::string topic = topicsValue[(int)i].asString();
                CHANNEL_LOG(TRACE) << "onClientTopicRequest insert" << LOG_KV("topic", topic);
                topics->insert(topic);
            }
        }
        session->setTopics(topics);

        updateHostTopics();
    }
    catch (exception& e)
    {
        CHANNEL_LOG(ERROR) << "onClientTopicRequest error"
                           << LOG_KV("what", boost::diagnostic_information(e));
    }
}

bool bcos::ChannelRPCServer::limitAMOPBandwidth(bcos::channel::ChannelSession::Ptr _session,
    bcos::channel::Message::Ptr _AMOPReq, bcos::p2p::P2PMessage::Ptr _p2pMessage)
{
    int64_t requiredPermitsAfterCompress = _p2pMessage->length() / g_BCOSConfig.c_compressRate;
    if (!m_networkBandwidthLimiter)
    {
        return true;
    }
    if (m_networkBandwidthLimiter->tryAcquire(requiredPermitsAfterCompress))
    {
        _p2pMessage->setPermitsAcquired(true);
        return true;
    }
    CHANNEL_LOG(INFO) << LOG_BADGE("limitAMOPBandwidth: over bandwidth limitation")
                      << LOG_KV("requiredPermitsAfterCompress", requiredPermitsAfterCompress)
                      << LOG_KV("maxPermitsPerSecond(Bytes)", m_networkBandwidthLimiter->maxQPS())
                      << LOG_KV("seq", _AMOPReq->seq().substr(0, c_seqAbridgedLen));
    // send REJECT_AMOP_REQ_FOR_OVER_BANDWIDTHLIMIT to client
    sendRejectAMOPResponse(_session, _AMOPReq);
    return false;
}

void bcos::ChannelRPCServer::sendRejectAMOPResponse(
    bcos::channel::ChannelSession::Ptr _session, bcos::channel::Message::Ptr _AMOPReq)
{
    CHANNEL_LOG(INFO) << LOG_BADGE("sendRejectAMOPResponse")
                      << LOG_DESC("Reject AMOP Request for over bandwidth limitation")
                      << LOG_KV("seq", _AMOPReq->seq().substr(0, c_seqAbridgedLen));
    auto response = _AMOPReq;
    response->clearData();
    response->setType(AMOP_RESPONSE);
    response->setResult(REJECT_AMOP_REQ_FOR_OVER_BANDWIDTHLIMIT);
    _session->asyncSendMessage(response, bcos::channel::ChannelSession::CallbackType(), 0);
}

void bcos::ChannelRPCServer::onClientChannelRequest(
    bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message)
{
    CHANNEL_LOG(DEBUG) << LOG_DESC("SDK channel2 request") << LOG_KV("type", message->type())
                       << LOG_KV("seq", message->seq());

    if (message->dataSize() < 1)
    {
        CHANNEL_LOG(ERROR) << "invalid channel2 message too short";
        return;
    }

    uint8_t topicLen = *((uint8_t*)message->data());
    std::string topic((char*)message->data() + 1, topicLen - 1);

    CHANNEL_LOG(DEBUG) << "target topic:" << topic;

    if (message->type() == AMOP_REQUEST)
    {
        try
        {
            CHANNEL_LOG(DEBUG) << "channel2 request"
                               << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen));

            auto buffer = std::make_shared<bytes>();
            message->encode(*buffer);

            auto p2pMessage = std::dynamic_pointer_cast<p2p::P2PMessage>(
                m_service->p2pMessageFactory()->buildMessage());
            p2pMessage->setBuffer(buffer);
            p2pMessage->setProtocolID(bcos::protocol::ProtocolID::AMOP);
            p2pMessage->setPacketType(0u);

            // Exceed the bandwidth-limit, return REJECT_AMOP_REQ_FOR_OVER_BANDWIDTHLIMIT AMOP
            // response
            if (!limitAMOPBandwidth(session, message, p2pMessage))
            {
                return;
            }
            bcos::network::Options options;
            options.timeout = 30 * 1000;  // 30 seconds

            m_service->asyncSendMessageByTopic(
                topic, p2pMessage,
                [session, message](bcos::network::NetworkException e,
                    std::shared_ptr<bcos::p2p::P2PSession>, bcos::p2p::P2PMessage::Ptr response) {
                    if (e.errorCode())
                    {
                        CHANNEL_LOG(WARNING)
                            << "ChannelMessage failed" << LOG_KV("errorCode", e.errorCode())
                            << LOG_KV("what", boost::diagnostic_information(e));
                        message->setType(AMOP_RESPONSE);
                        message->setResult(REMOTE_PEER_UNAVAILABLE);
                        message->clearData();

                        session->asyncSendMessage(
                            message, bcos::channel::ChannelSession::CallbackType(), 0);

                        CHANNEL_LOG(DEBUG)
                            << "channel2 send response"
                            << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen));
                        return;
                    }

                    message->decode(response->buffer()->data(), response->buffer()->size());

                    session->asyncSendMessage(
                        message, bcos::channel::ChannelSession::CallbackType(), 0);
                },
                options);
        }
        catch (exception& e)
        {
            CHANNEL_LOG(ERROR) << "send error" << LOG_KV("what", boost::diagnostic_information(e));

            message->setType(AMOP_RESPONSE);
            message->setResult(REMOTE_PEER_UNAVAILABLE);
            message->clearData();

            session->asyncSendMessage(message, bcos::channel::ChannelSession::CallbackType(), 0);
        }
    }
    else if (message->type() == AMOP_MULBROADCAST)
    {
        // client multicast message
        try
        {
            CHANNEL_LOG(DEBUG) << "channel2 multicast request"
                               << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen));
            auto buffer = std::make_shared<bytes>();

            message->encode(*buffer);

            auto p2pMessage = std::dynamic_pointer_cast<p2p::P2PMessage>(
                m_service->p2pMessageFactory()->buildMessage());
            p2pMessage->setBuffer(buffer);
            p2pMessage->setProtocolID(bcos::protocol::ProtocolID::AMOP);
            p2pMessage->setPacketType(1u);

            // Exceed the bandwidth limit, return REJECT_AMOP_REQ_FOR_OVER_BANDWIDTHLIMIT AMOP
            // response
            bool sended = m_service->asyncMulticastMessageByTopic(
                topic, p2pMessage, m_networkBandwidthLimiter);
            if (!sended)
            {
                sendRejectAMOPResponse(session, message);
                return;
            }
            message->setType(AMOP_RESPONSE);
            message->setResult(0);
            session->asyncSendMessage(message, bcos::channel::ChannelSession::CallbackType(), 0);
        }
        catch (exception& e)
        {
            CHANNEL_LOG(ERROR) << "send error" << LOG_KV("what", boost::diagnostic_information(e));

            message->setType(AMOP_RESPONSE);
            message->setResult(REMOTE_PEER_UNAVAILABLE);
            message->clearData();

            session->asyncSendMessage(message, bcos::channel::ChannelSession::CallbackType(), 0);
        }
    }
    else
    {
        CHANNEL_LOG(WARNING) << "unknown message type" << LOG_KV("type", message->type());
    }
}

void ChannelRPCServer::setListenAddr(const std::string& listenAddr)
{
    _listenAddr = listenAddr;
}

void ChannelRPCServer::setListenPort(int listenPort)
{
    _listenPort = listenPort;
}

void ChannelRPCServer::CloseConnection(int _socket)
{
    close(_socket);
}

void ChannelRPCServer::setService(std::shared_ptr<bcos::p2p::P2PInterface> _service)
{
    m_service = _service;
}

void ChannelRPCServer::setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext)
{
    _sslContext = sslContext;
}

void ChannelRPCServer::setChannelServer(std::shared_ptr<bcos::channel::ChannelServer> server)
{
    _server = server;
}

void ChannelRPCServer::asyncPushChannelMessage(std::string topic,
    bcos::channel::Message::Ptr message,
    std::function<void(bcos::channel::ChannelException, bcos::channel::Message::Ptr,
        bcos::channel::ChannelSession::Ptr)>
        callback)
{
    try
    {
        class Callback : public std::enable_shared_from_this<Callback>
        {
        public:
            typedef std::shared_ptr<Callback> Ptr;

            Callback(std::string topic, bcos::channel::Message::Ptr message,
                ChannelRPCServer::Ptr server,
                std::function<void(bcos::channel::ChannelException, bcos::channel::Message::Ptr,
                    bcos::channel::ChannelSession::Ptr)>
                    callback)
              : m_topic(topic), m_message(message), m_server(server), m_callback(callback){};

            void onResponse(bcos::channel::ChannelException e, bcos::channel::Message::Ptr message)
            {
                try
                {
                    if (e.errorCode() != 0)
                    {
                        CHANNEL_LOG(WARNING)
                            << "onResponse error" << LOG_KV("errorCode", e.errorCode())
                            << LOG_KV("what", boost::diagnostic_information(e));

                        m_exclude.insert(m_currentSession);

                        sendMessage();

                        return;
                    }
                }
                catch (bcos::channel::ChannelException& ex)
                {
                    CHANNEL_LOG(WARNING)
                        << "onResponse error" << LOG_KV("errorCode", ex.errorCode())
                        << LOG_KV("what", ex.what());

                    try
                    {
                        if (m_callback)
                        {
                            m_callback(ex, bcos::channel::Message::Ptr(), m_currentSession);
                        }
                    }
                    catch (exception& e)
                    {
                        CHANNEL_LOG(WARNING) << "onResponse error"
                                             << LOG_KV("what", boost::diagnostic_information(e));
                    }

                    return;
                }

                try
                {
                    if (m_callback)
                    {
                        m_callback(e, message, m_currentSession);
                    }
                }
                catch (exception& e)
                {
                    CHANNEL_LOG(WARNING)
                        << "onResponse error" << LOG_KV("what", boost::diagnostic_information(e));
                }
            }

            void sendMessage()
            {
                std::vector<bcos::channel::ChannelSession::Ptr> activedSessions =
                    m_server->getSessionByTopic(m_topic);

                if (activedSessions.empty())
                {
                    CHANNEL_LOG(ERROR)
                        << "sendMessage failed: no session use topic" << LOG_KV("topic", m_topic);
                    throw bcos::channel::ChannelException(104, "no session use topic:" + m_topic);
                }

                for (auto sessionIt = activedSessions.begin(); sessionIt != activedSessions.end();)
                {
                    if (m_exclude.find(*sessionIt) != m_exclude.end())
                    {
                        sessionIt = activedSessions.erase(sessionIt);
                    }
                    else
                    {
                        ++sessionIt;
                    }
                }

                if (activedSessions.empty())
                {
                    CHANNEL_LOG(ERROR) << "all session try failed";

                    throw bcos::channel::ChannelException(104, "all session failed");
                }

                boost::mt19937 rng(static_cast<unsigned>(std::time(0)));
                boost::uniform_int<int> index(0, activedSessions.size() - 1);

                auto ri = index(rng);
                CHANNEL_LOG(TRACE)
                    << "random node" << ri << " active session size:" << activedSessions.size();

                auto session = activedSessions[ri];

                std::function<void(bcos::channel::ChannelException, bcos::channel::Message::Ptr)>
                    fp = std::bind(&Callback::onResponse, shared_from_this(), std::placeholders::_1,
                        std::placeholders::_2);
                session->asyncSendMessage(m_message, fp, 5000);

                CHANNEL_LOG(INFO) << "Push channel message success"
                                  << LOG_KV("seq", m_message->seq().substr(0, c_seqAbridgedLen))
                                  << LOG_KV("session", session->host()) << ":" << session->port();
                m_currentSession = session;
            }

        private:
            std::string m_topic;
            bcos::channel::Message::Ptr m_message;
            ChannelRPCServer::Ptr m_server;
            bcos::channel::ChannelSession::Ptr m_currentSession;
            std::set<bcos::channel::ChannelSession::Ptr> m_exclude;
            std::function<void(bcos::channel::ChannelException, bcos::channel::Message::Ptr,
                bcos::channel::ChannelSession::Ptr)>
                m_callback;
        };

        Callback::Ptr pushCallback =
            std::make_shared<Callback>(topic, message, shared_from_this(), callback);
        pushCallback->sendMessage();
    }
    catch (bcos::channel::ChannelException& ex)
    {
        callback(ex, bcos::channel::Message::Ptr(), bcos::channel::ChannelSession::Ptr());
    }
    catch (exception& e)
    {
        CHANNEL_LOG(ERROR) << "asyncPushChannelMessage error"
                           << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelRPCServer::asyncBroadcastChannelMessage(
    std::string topic, bcos::channel::Message::Ptr message)
{
    std::vector<bcos::channel::ChannelSession::Ptr> activedSessions = getSessionByTopic(topic);
    if (activedSessions.empty())
    {
        CHANNEL_LOG(TRACE) << "no session use topic" << LOG_KV("topic", topic);
        return;
    }

    for (auto session : activedSessions)
    {
        session->asyncSendMessage(
            message, std::function<void(bcos::channel::ChannelException, Message::Ptr)>(), 0);

        CHANNEL_LOG(INFO) << "Push channel message success" << LOG_KV("topic", topic)
                          << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen))
                          << LOG_KV("session", session->host()) << ":" << session->port();
    }
}

bcos::channel::TopicChannelMessage::Ptr ChannelRPCServer::pushChannelMessage(
    bcos::channel::TopicChannelMessage::Ptr message, size_t timeout)
{
    try
    {
        std::string topic = message->topic();

        CHANNEL_LOG(TRACE) << "Push to SDK" << LOG_KV("topic", topic)
                           << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen));
        std::vector<bcos::channel::ChannelSession::Ptr> activedSessions = getSessionByTopic(topic);

        if (activedSessions.empty())
        {
            CHANNEL_LOG(ERROR) << "no SDK follow topic" << LOG_KV("topic", topic);

            BOOST_THROW_EXCEPTION(
                ChannelException(103, "send failed, no node follow topic:" + topic));
        }

        bcos::channel::TopicChannelMessage::Ptr response;
        for (auto& it : activedSessions)
        {
            bcos::channel::Message::Ptr responseMessage = it->sendMessage(message, timeout);

            if (responseMessage.get() != NULL && responseMessage->result() == 0)
            {
                response = std::make_shared<TopicChannelMessage>(responseMessage.get());
                break;
            }
        }

        if (!response)
        {
            BOOST_THROW_EXCEPTION(ChannelException(99, "send fail, all retry failed"));
        }

        return response;
    }
    catch (bcos::channel::ChannelException& e)
    {
        CHANNEL_LOG(ERROR) << "pushChannelMessage error" << LOG_KV("what", e.what());
        BOOST_THROW_EXCEPTION(e);
    }

    return bcos::channel::TopicChannelMessage::Ptr();
}

void ChannelRPCServer::updateHostTopics()
{
    auto allTopics = std::make_shared<std::set<std::string> >();
    std::lock_guard<std::mutex> lock(_sessionMutex);
    for (auto& it : _sessions)
    {
        auto topics = it.second->topics();
        for (auto topic : topics)
        {
            allTopics->insert(topic);
        }
    }
    m_service->setTopics(allTopics);
}

std::vector<bcos::channel::ChannelSession::Ptr> ChannelRPCServer::getSessionByTopic(
    const std::string& topic)
{
    std::vector<bcos::channel::ChannelSession::Ptr> activedSessions;

    std::lock_guard<std::mutex> lock(_sessionMutex);
    for (auto& it : _sessions)
    {
        if (it.second->topics().empty() || !it.second->actived())
        {
            continue;
        }

        auto topics = it.second->topics();
        auto topicIt = topics.find(topic);
        if (topicIt != topics.end())
        {
            activedSessions.push_back(it.second);
        }
    }

    return activedSessions;
}

void ChannelRPCServer::registerSDKAllowListByGroupId(
    bcos::GROUP_ID const& _groupId, bcos::PeerWhitelist::Ptr _allowList)
{
    CHANNEL_LOG(INFO) << LOG_DESC("registerSDKAllowListByGroupId") << LOG_KV("groupId", _groupId)
                      << LOG_KV("size", _allowList->size());
    WriteGuard l(x_group2SDKAllowList);
    if (_allowList->size() == 0)
    {
        CHANNEL_LOG(INFO) << LOG_DESC(
            "Disable group-level sdk permission control for sdk allowlist is empty");
        if (m_group2SDKAllowList->count(_groupId))
        {
            m_group2SDKAllowList->erase(_groupId);
        }
        return;
    }
    (*m_group2SDKAllowList)[_groupId] = _allowList;
}

void ChannelRPCServer::removeSDKAllowListByGroupId(bcos::GROUP_ID const& _groupId)
{
    if (!m_group2SDKAllowList)
    {
        return;
    }
    CHANNEL_LOG(INFO) << LOG_DESC("removeSDKAllowListByGroupId") << LOG_KV("groupId", _groupId);
    UpgradableGuard l(x_group2SDKAllowList);
    if (m_group2SDKAllowList->count(_groupId))
    {
        UpgradeGuard ul(l);
        m_group2SDKAllowList->erase(_groupId);
    }
}

bool ChannelRPCServer::checkSDKPermission(bcos::GROUP_ID _groupId, bcos::h512 const& _sdkPublicKey)
{
    if (-1 == _groupId)
    {
        return true;
    }
    // SDK allowlist is not set
    auto allowList = getSDKAllowListByGroupId(_groupId);
    if (!allowList)
    {
        return true;
    }
    // check if the requesting SDK is on the allowlist
    if (allowList->has(_sdkPublicKey))
    {
        return true;
    }
    return false;
}
