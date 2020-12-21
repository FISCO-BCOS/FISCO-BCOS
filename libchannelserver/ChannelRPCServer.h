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
 * @file: ChannelRPCServer.h
 * @author: monan
 * @date: 2017
 *
 * @author xingqiangbai
 * @date 2018.11.5
 * @modify use libp2p to send message
 */

#pragma once

#include "ChannelMessage.h"  // for TopicChannelM...
#include "ChannelSession.h"  // for ChannelSessio...
#include "Message.h"         // for Message, Mess...
#include "libp2p/P2PMessage.h"
#include <jsonrpccpp/server/abstractserverconnector.h>
#include <libflowlimit/RPCQPSLimiter.h>
#include <libnetwork/PeerWhitelist.h>
#include <libstat/ChannelNetworkStatHandler.h>
#include <boost/asio/io_service.hpp>  // for io_service
#include <atomic>                     // for atomic
#include <map>                        // for map
#include <mutex>                      // for mutex
#include <set>                        // for set
#include <string>                     // for string
#include <thread>
#include <utility>  // for swap, move
#include <vector>   // for vector
namespace boost
{
namespace asio
{
namespace ssl
{
class context;
}
}  // namespace asio
}  // namespace boost

namespace Json
{
class Value;
}  // namespace Json

namespace bcos
{
namespace channel
{
class ChannelException;
class ChannelServer;
}  // namespace channel
namespace network
{
class NetworkException;
}
namespace p2p
{
class P2PInterface;
class P2PMessage;
class P2PSession;
}  // namespace p2p

class ChannelRPCServer : public jsonrpc::AbstractServerConnector,
                         public std::enable_shared_from_this<ChannelRPCServer>
{
public:
    enum ChannelERRORCODE
    {
        REMOTE_PEER_UNAVAILABLE = 100,
        REMOTE_CLIENT_PEER_UNAVAILABLE = 101,
        TIMEOUT = 102,
        REJECT_AMOP_REQ_FOR_OVER_BANDWIDTHLIMIT = 103
    };
    typedef std::shared_ptr<ChannelRPCServer> Ptr;

    ChannelRPCServer(std::string listenAddr = "", int listenPort = 0)
      : jsonrpc::AbstractServerConnector(),
        _listenAddr(listenAddr),
        _listenPort(listenPort),
        m_group2SDKAllowList(
            std::make_shared<std::map<bcos::GROUP_ID, bcos::PeerWhitelist::Ptr>>()){};

    virtual ~ChannelRPCServer();
    virtual bool StartListening() override;
    virtual bool StopListening() override;
    virtual bool SendResponse(std::string const& _response, void* _addInfo = nullptr) override;


    virtual void onConnect(
        bcos::channel::ChannelException e, bcos::channel::ChannelSession::Ptr session);


    virtual void onDisconnect(
        bcos::channel::ChannelException e, bcos::channel::ChannelSession::Ptr session);


    virtual void onClientRequest(bcos::channel::ChannelSession::Ptr session,
        bcos::channel::ChannelException e, bcos::channel::Message::Ptr message);
    virtual void blockNotify(int16_t _groupID, int64_t _blockNumber);

    void setListenAddr(const std::string& listenAddr);

    void setListenPort(int listenPort);

    void removeSession(int sessionID);

    void CloseConnection(int _socket);

    void onNodeChannelRequest(bcos::network::NetworkException, std::shared_ptr<p2p::P2PSession>,
        std::shared_ptr<p2p::P2PMessage>);

    void setService(std::shared_ptr<bcos::p2p::P2PInterface> _service);

    void setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext);

    std::shared_ptr<bcos::channel::ChannelServer> channelServer() { return _server; }
    void setChannelServer(std::shared_ptr<bcos::channel::ChannelServer> server);

    void asyncPushChannelMessage(std::string topic, bcos::channel::Message::Ptr message,
        std::function<void(bcos::channel::ChannelException, bcos::channel::Message::Ptr,
            bcos::channel::ChannelSession::Ptr)>
            callback);

    void asyncPushChannelMessageHandler(const std::string& toTopic, const std::string& content);

    void asyncBroadcastChannelMessage(std::string topic, bcos::channel::Message::Ptr message);

    virtual bcos::channel::TopicChannelMessage::Ptr pushChannelMessage(
        bcos::channel::TopicChannelMessage::Ptr message, size_t timeout);

    void setCallbackSetter(std::function<void(
            std::function<void(const std::string& receiptContext, GROUP_ID _groupId)>*,
            std::function<uint32_t()>*)>
            callbackSetter)
    {
        m_callbackSetter = callbackSetter;
    };

    void setEventFilterCallback(std::function<int32_t(const std::string&, uint32_t,
            std::function<bool(const std::string& _filterID, int32_t _result,
                const Json::Value& _logs, GROUP_ID const& _groupId)>,
            std::function<int(GROUP_ID _groupId)>, std::function<bool(GROUP_ID _groupId)>)>
            _callback)
    {
        m_eventFilterCallBack = _callback;
    };

    void setEventCancelFilterCallback(
        std::function<int32_t(const std::string&, uint32_t, std::function<bool(GROUP_ID _groupId)>)>
            _callback)
    {
        m_eventCancelFilterCallBack = _callback;
    };
    void setNetworkStatHandler(bcos::stat::ChannelNetworkStatHandler::Ptr _handler)
    {
        m_networkStatHandler = _handler;
    }

    bcos::stat::ChannelNetworkStatHandler::Ptr networkStatHandler() { return m_networkStatHandler; }

    void setQPSLimiter(bcos::flowlimit::RPCQPSLimiter::Ptr _qpsLimiter)
    {
        m_qpsLimiter = _qpsLimiter;
    }

    bcos::flowlimit::RPCQPSLimiter::Ptr qpsLimiter() { return m_qpsLimiter; }

    void setNetworkBandwidthLimiter(bcos::flowlimit::RateLimiter::Ptr _networkBandwidthLimiter)
    {
        m_networkBandwidthLimiter = _networkBandwidthLimiter;
    }

    bcos::flowlimit::RateLimiter::Ptr networkBandwidthLimiter() const
    {
        return m_networkBandwidthLimiter;
    }

    void registerSDKAllowListByGroupId(
        bcos::GROUP_ID const& _groupId, bcos::PeerWhitelist::Ptr _allowList);

    // remove the registered sdk allowlist when stop/delete the group
    void removeSDKAllowListByGroupId(bcos::GROUP_ID const& _groupId);

private:
    bool checkSDKPermission(bcos::GROUP_ID _groupId, bcos::h512 const& _sdkPublicKey);

    bcos::PeerWhitelist::Ptr getSDKAllowListByGroupId(bcos::GROUP_ID const& _groupId)
    {
        ReadGuard l(x_group2SDKAllowList);
        if (m_group2SDKAllowList->count(_groupId))
        {
            return (*m_group2SDKAllowList)[_groupId];
        }
        return nullptr;
    }

    bool OnRpcRequest(
        bcos::channel::ChannelSession::Ptr _session, const std::string& request, void* addInfo);

    virtual void onClientRPCRequest(
        bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message);

    virtual void onClientTopicRequest(
        bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message);

    virtual void onClientChannelRequest(
        bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message);

    virtual void onClientRegisterEventLogRequest(
        bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message);

    virtual void onClientUnregisterEventLogRequest(
        bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message);

    virtual void onClientHandshake(
        bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message);

    virtual void onClientHeartbeat(
        bcos::channel::ChannelSession::Ptr session, bcos::channel::Message::Ptr message);

    bcos::channel::ChannelSession::Ptr sendChannelMessageToSession(std::string topic,
        bcos::channel::Message::Ptr message,
        const std::set<bcos::channel::ChannelSession::Ptr>& exclude);

    void updateHostTopics();

    std::vector<bcos::channel::ChannelSession::Ptr> getSessionByTopic(const std::string& topic);

    void onClientUpdateTopicStatusRequest(bcos::channel::Message::Ptr message);
    bool limitAMOPBandwidth(bcos::channel::ChannelSession::Ptr _session,
        bcos::channel::Message::Ptr _AMOPReq, bcos::p2p::P2PMessage::Ptr _p2pMessage);

    void sendRejectAMOPResponse(
        bcos::channel::ChannelSession::Ptr _session, bcos::channel::Message::Ptr _AMOPReq);

    bool _running = false;

    std::string _listenAddr;
    int _listenPort;
    std::shared_ptr<boost::asio::io_service> _ioService;

    std::shared_ptr<boost::asio::ssl::context> _sslContext;
    std::shared_ptr<bcos::channel::ChannelServer> _server;

    std::map<int, bcos::channel::ChannelSession::Ptr> _sessions;
    std::mutex _sessionMutex;

    std::map<std::string, bcos::channel::ChannelSession::Ptr> _seq2session;
    std::mutex _seqMutex;

    int _sessionCount = 1;

    std::shared_ptr<bcos::p2p::P2PInterface> m_service;

    std::function<void(std::function<void(const std::string& receiptContext, GROUP_ID _groupId)>*,
        std::function<uint32_t()>*)>
        m_callbackSetter;

    std::function<int32_t(const std::string&, uint32_t,
        std::function<bool(const std::string& _filterID, int32_t _result, const Json::Value& _logs,
            GROUP_ID const& _groupId)>,
        std::function<int(GROUP_ID _groupId)>, std::function<bool(GROUP_ID _groupId)>)>
        m_eventFilterCallBack;

    std::function<int32_t(const std::string&, uint32_t, std::function<bool(GROUP_ID _groupId)>)>
        m_eventCancelFilterCallBack;

    bcos::stat::ChannelNetworkStatHandler::Ptr m_networkStatHandler;
    bcos::flowlimit::RPCQPSLimiter::Ptr m_qpsLimiter;
    bcos::flowlimit::RateLimiter::Ptr m_networkBandwidthLimiter;

    std::shared_ptr<std::map<bcos::GROUP_ID, bcos::PeerWhitelist::Ptr>> m_group2SDKAllowList;
    mutable SharedMutex x_group2SDKAllowList;
};

}  // namespace bcos
